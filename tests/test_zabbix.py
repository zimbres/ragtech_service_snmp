"""Validate generated monitoring metadata and installer verification logic."""
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'zabbix'))
from generate_template import build
from install_template import install, ApiError, ZabbixApi
from test_snmp import request,decode
ROOT=Path(__file__).resolve().parents[1]

class TemplateTests(unittest.TestCase):
    def setUp(self):
        self.export=json.loads((ROOT/'zabbix/ragtech_ups_snmp.json').read_text())['zabbix_export']
        self.template=self.export['templates'][0]
    def test_reproducible_and_complete(self):
        self.assertEqual(build()['zabbix_export'],self.export)
        items=self.template['items']
        self.assertEqual(len(items),52)
        self.assertEqual(len({x['key'] for x in items}),52)
        self.assertEqual(len({x['snmp_oid'] for x in items}),52)
        uuids=[self.template['uuid']]+[obj['uuid'] for field in ('items','valuemaps') for obj in self.template[field]]
        uuids += [obj['uuid'] for field in ('triggers','graphs') for obj in self.export[field]]
        self.assertEqual(len(uuids),len(set(uuids)))
        self.assertTrue(all(re.fullmatch('[a-f0-9]{32}',u) for u in uuids))
        names={m['name'] for m in self.template['valuemaps']}
        for item in items:
            if 'valuemap' in item:self.assertIn(item['valuemap']['name'],names)
        trigger_names={t['name'] for t in self.export['triggers']}
        for trigger in self.export['triggers']:
            for dependency in trigger.get('dependencies',[]):
                self.assertIn(dependency['name'],trigger_names)
            for key in re.findall(r'/Ragtech UPS by SNMP/([^,)]+)',trigger['expression']):
                self.assertIn(key,{i['key'] for i in items})
    def test_oid_types_scaling_against_agent(self):
        proc=subprocess.Popen([sys.argv[1],'1','4'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        values={}
        try:
            for item in self.template['items']:
                proc.stdin.write(request([item['snmp_oid']]).hex()+'\n');proc.stdin.flush()
                result=decode(bytes.fromhex(proc.stdout.readline().strip()))[2][0]
                self.assertEqual(result[0],item['snmp_oid'])
                self.assertEqual(result[1],4 if item['value_type'] in ('CHAR','TEXT') else 2)
                value=result[2]
                steps=item.get('preprocessing',[])
                if steps:value*=float(steps[0]['parameters'][0])
                values[item['key']]=value
        finally:
            proc.stdin.close();proc.wait(timeout=3);proc.stdout.close()
        for key,value in [('battery_voltage',13.6),('output_frequency',59.7),('output_current',2.5),('output_power',276)]:
            self.assertAlmostEqual(values['ragtech.ups.'+key],value)
        self.assertAlmostEqual(values['ragtech.ext.ragtechUpsBatteryVoltage'],13.6)
        self.assertAlmostEqual(values['ragtech.ext.ragtechUpsOutputFrequency'],59.71)
        self.assertAlmostEqual(values['ragtech.ext.ragtechUpsOutputCurrent'],2.52)
        self.assertAlmostEqual(values['ragtech.ext.ragtechUpsOutputPower'],276.38)
        self.assertEqual(values['ragtech.ups.output_load'],32)
    def test_installer_rules_and_verification(self):
        export=self.export;template=self.template
        class FakeApi:
            def call(self,method,params):
                if method=='configuration.import':
                    self.rules=params['rules'];return True
                if method=='template.get':return [{'templateid':'123','host':template['template']}]
                if method=='item.get':
                    types={'FLOAT':'0','CHAR':'1','UNSIGNED':'3','TEXT':'4'}
                    return [{'key_':i['key'],'snmp_oid':i['snmp_oid'],'value_type':types[i['value_type']],
                             'preprocessing':[{'type':'1','params':s['parameters'][0]} for s in i.get('preprocessing',[])]} for i in template['items']]
                if method=='trigger.get':return [{'description':i['name']} for i in export['triggers']]
                if method=='graph.get':return [{'name':i['name']} for i in export['graphs']]
                raise AssertionError(method)
        for release,group in [((6,0),'groups'),((7,0),'template_groups'),((7,2),'template_groups')]:
            api=FakeApi()
            self.assertEqual(install(api,json.dumps({'zabbix_export':export}),release),('123',52,10,3))
            self.assertIn(group,api.rules)
            self.assertTrue(all(not r.get('deleteMissing',False) for r in api.rules.values()))
    def test_https_only(self):
        for url in ['http://example.org','https://user:password@example.org','https://example.org/?token=x']:
            with self.assertRaises(ApiError):ZabbixApi(url)

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
