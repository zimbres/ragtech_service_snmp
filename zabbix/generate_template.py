#!/usr/bin/env python3
"""Generate a dependency-free Zabbix 6.0+ JSON template from the agent MIB."""
import hashlib
import json
import re
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = 'Ragtech UPS by SNMP'
GROUP = 'Templates/Power'
BASE = '1.3.6.1.2.1.33.1.'
EXT = '1.3.6.1.3.1628.1.'

def uid(name):
    return uuid.UUID(bytes=hashlib.md5(('ragtech_service:'+name).encode()).digest(), version=4).hex

def build():
    items=[]
    def item(key,name,oid,units='',multiplier=None,value_type='UNSIGNED',mapping=None,description='',component='ups'):
        obj={'uuid':uid(key),'name':name,'type':'SNMP_AGENT','snmp_oid':oid,'key':key,
             'delay':'{$UPS.POLL.INTERVAL}','history':'7d','trends':'365d',
             'value_type':value_type,'description':description,
             'tags':[{'tag':'component','value':component}]}
        if units:obj['units']=units
        if multiplier:
            obj['preprocessing']=[{'type':'MULTIPLIER','parameters':[multiplier]}]
        if value_type in ('CHAR','TEXT'):obj['trends']='0'
        if mapping:obj['valuemap']={'name':mapping}
        items.append(obj)
        return obj
    standard=[
        ('manufacturer','Manufacturer','1.1.0','','CHAR',None,None),
        ('model','Model','1.2.0','','CHAR',None,None),
        ('agent_version','Agent software version','1.4.0','','CHAR',None,None),
        ('battery_status','Battery status','2.1.0','','UNSIGNED',None,'UPS battery status'),
        ('seconds_on_battery','Seconds on battery','2.2.0','s','UNSIGNED',None,None),
        ('runtime','Estimated runtime remaining','2.3.0','min','UNSIGNED',None,None),
        ('charge','Estimated battery charge','2.4.0','%','UNSIGNED',None,None),
        ('battery_voltage','Battery voltage','2.5.0','V','FLOAT','0.1',None),
        ('input_lines','Input line count','3.2.0','','UNSIGNED',None,None),
        ('input_voltage','Input voltage, line 1','3.3.1.3.1','V','UNSIGNED',None,None),
        ('output_source','Output source','4.1.0','','UNSIGNED',None,'UPS output source'),
        ('output_frequency','Output frequency','4.2.0','Hz','FLOAT','0.1',None),
        ('output_lines','Output line count','4.3.0','','UNSIGNED',None,None),
        ('output_voltage','Output voltage, line 1','4.4.1.2.1','V','UNSIGNED',None,None),
        ('output_current','Output current, line 1','4.4.1.3.1','A','FLOAT','0.1',None),
        ('output_power','Output power estimate, line 1','4.4.1.4.1','W','UNSIGNED',None,None),
        ('output_load','Output load, line 1','4.4.1.5.1','%','UNSIGNED',None,None)]
    for key,name,oid,units,vt,multi,mapping in standard:
        obj=item('ragtech.ups.'+key,'UPS: '+name,BASE+oid,units,multi,vt,mapping,
                 'RFC1628 UPS-MIB. Telemetry can be unavailable when the UPS is disconnected.','rfc1628')
        if vt=='CHAR':obj['delay']='1h'
        if key=='runtime':obj['description']+=' No estimate is available at zero load; the agent returns noSuchInstance.'
        if key=='output_power':obj['description']+=' Estimated as voltage times current; not measured real power.'
    mib=(ROOT/'mibs/RAGTECH-UPS-MIB.txt').read_text()
    entries=re.findall(r'(\w+) OBJECT-TYPE\s+(.*?)::=\s*\{\s*(\w+)\s+(\d+)\s*\}',mib,re.S)
    units=['%','A','V','V','W','W','kWh','kWh','°C','%','V','Hz','min']
    for symbol,body,parent,index in entries:
        desc=re.search(r'DESCRIPTION\s+"(.*?)"',body,re.S).group(1)
        desc=' '.join(desc.split())
        title=re.sub(r'(?<=[a-z])(?=[A-Z])',' ',symbol.removeprefix('ragtechUps'))
        if parent=='ragtechUpsMetrics':
            item('ragtech.ext.'+symbol,'UPS detailed: '+title,EXT+'1.'+index+'.0',
                 units[int(index)-1],'0.01','FLOAT',description=desc,component='metrics')
        elif parent=='ragtechUpsFlags':
            item('ragtech.flag.'+symbol,'UPS flag: '+title,EXT+'2.'+index+'.0',mapping='UPS flag',
                 description=desc,component='status')
        elif symbol=='ragtechUpsConnected':
            item('ragtech.ups.connected','UPS: Serial connection state',EXT+'3.0',mapping='UPS connection',
                 description=desc,component='availability')
        else:
            obj=item('ragtech.ups.raw_packet','UPS: Last raw response',EXT+'4.0',value_type='TEXT',
                     description=desc,component='diagnostics');obj['history']='1d';obj['delay']='5m'
    assert len(items)==52
    def mapping(name,values):
        return {'uuid':uid('map:'+name),'name':name,
                'mappings':[{'value':str(k),'newvalue':v} for k,v in values.items()]}
    valuemaps=[mapping('UPS flag',{0:'Inactive',1:'Active'}),
               mapping('UPS connection',{0:'Disconnected',1:'Connected'}),
               mapping('UPS battery status',{1:'Unknown',2:'Normal',3:'Low',4:'Depleted'}),
               mapping('UPS output source',{1:'Other / unknown',2:'None',3:'Mains',4:'Bypass',5:'Battery',6:'Booster',7:'Reducer'})]
    connected=f'last(/{NAME}/ragtech.ups.connected)=1'
    def last(key):return f'last(/{NAME}/{key})'
    def trigger(label,expression,priority,depends=False):
        t={'uuid':uid('trigger:'+label),'name':'UPS: '+label,'expression':expression,'priority':priority,
           'tags':[{'tag':'scope','value':'availability' if 'communication' in label else 'notice'}]}
        if depends:t['dependencies']=[{'name':'UPS: Serial communication lost',
            'expression':f'max(/{NAME}/ragtech.ups.connected,2m)=0'}]
        return t
    triggers=[trigger('Serial communication lost',f'max(/{NAME}/ragtech.ups.connected,2m)=0','HIGH'),
              trigger('SNMP communication unavailable',f'nodata(/{NAME}/ragtech.ups.connected,{{$UPS.NODATA.TIMEOUT}})=1','HIGH'),
              trigger('Running on battery',f'{last("ragtech.ups.output_source")}=5 and {connected}','WARNING',True),
              trigger('Battery low',f'{last("ragtech.ups.battery_status")}=3 and {connected}','HIGH',True),
              trigger('Battery depleted',f'{last("ragtech.ups.battery_status")}=4 and {connected}','DISASTER',True),
              trigger('Output load high',f'{last("ragtech.ups.output_load")}>{{$UPS.LOAD.WARN}} and {connected}','WARNING',True)]
    for symbol,label in [('ragtechUpsOvertemperatureFailure','Overtemperature failure'),
                         ('ragtechUpsOverloadFailure','Overload failure'),
                         ('ragtechUpsInverterFailure','Inverter failure'),
                         ('ragtechUpsShortCircuitFailure','Short circuit failure')]:
        triggers.append(trigger(label,f'{last("ragtech.flag."+symbol)}=1 and {connected}','HIGH',True))
    graphs=[]
    for title,keys in [('Voltages',['input_voltage','output_voltage','battery_voltage']),
                       ('Load',['output_load','charge']),('Output power',['output_power'])]:
        graphs.append({'uuid':uid('graph:'+title),'name':'UPS: '+title,
                       'graph_items':[{'sortorder':str(i),'color':['199C0D','F63100','2774A4'][i],
                           'item':{'host':NAME,'key':'ragtech.ups.'+key}} for i,key in enumerate(keys)]})
    template={'uuid':uid('template'),'template':NAME,'name':NAME,
              'description':'Ragtech Family 10 agent: RFC1628 plus all RAGTECH-UPS-MIB extensions. Numeric OIDs require no MIB installation on the Zabbix server. SNMP v2c interface must be configured on the host. Extensions retain stale samples on disconnection. Power is an estimate, not measured real power.',
              'groups':[{'name':GROUP}],'items':items,'valuemaps':valuemaps,
              'tags':[{'tag':'class','value':'power'},{'tag':'target','value':'ragtech'}],
              'macros':[{'macro':'{$UPS.POLL.INTERVAL}','value':'30s','description':'UPS polling interval.'},
                        {'macro':'{$UPS.NODATA.TIMEOUT}','value':'5m','description':'SNMP data absence threshold.'},
                        {'macro':'{$UPS.LOAD.WARN}','value':'80','description':'Output load warning threshold in percent.'}]}
    return {'zabbix_export':{'version':'6.0','groups':[{'uuid':uid('group'),'name':GROUP}],
                             'templates':[template],'triggers':triggers,'graphs':graphs}}

if __name__=='__main__':
    target=ROOT/'zabbix/ragtech_ups_snmp.json'
    target.write_text(json.dumps(build(),indent=2,ensure_ascii=False)+'\n')
    print('Generated '+str(target))
