#!/usr/bin/env python3
"""Import and verify the Ragtech template using the standard-library Zabbix API."""
import argparse
import getpass
import json
import os
import sys
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler

class ApiError(RuntimeError):
    pass

class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ApiError('API endpoint redirected; specify its final HTTPS URL.')

class ZabbixApi:
    def __init__(self,url):
        parts=urlsplit(url)
        if parts.scheme!='https' or not parts.hostname or parts.username or parts.password or parts.query or parts.fragment:
            raise ApiError('Use an HTTPS Zabbix URL without credentials, query or fragment.')
        self.url=url.rstrip('/')
        if not self.url.endswith('/api_jsonrpc.php'):self.url+='/api_jsonrpc.php'
        self.token=None
        self.bearer=False
        self.opener=build_opener(NoRedirect())
        self.request_id=0

    def call(self,method,params):
        self.request_id+=1
        payload={'jsonrpc':'2.0','method':method,'params':params,'id':self.request_id}
        headers={'Content-Type':'application/json-rpc'}
        if self.token:
            if self.bearer:headers['Authorization']='Bearer '+self.token
            else:payload['auth']=self.token
        req=Request(self.url,json.dumps(payload).encode(),headers,method='POST')
        try:
            with self.opener.open(req,timeout=20) as response:
                data=json.load(response)
        except (URLError,ValueError) as exc:
            raise ApiError('Cannot reach or parse the Zabbix API: '+str(exc)) from None
        if 'error' in data:
            error=data['error']
            raise ApiError(f'{method}: {error.get("message", "API error")}: {error.get("data", "")}')
        if 'result' not in data:raise ApiError('API returned no result.')
        return data['result']

    def version(self):
        version=self.call('apiinfo.version',{})
        try:release=tuple(map(int,version.split('.')[:2]))
        except (ValueError,AttributeError):raise ApiError('Unrecognized Zabbix version.') from None
        if release<(6,0):raise ApiError('This template requires Zabbix 6.0 or newer.')
        self.bearer=release>=(7,2)
        return version,release


def install(api,source,release):
    export=json.loads(source)['zabbix_export']
    template=export['templates'][0]
    rules={name:{'createMissing':True,'updateExisting':True} for name in
           ('templates','items','triggers','graphs','valueMaps')}
    rules['template_groups' if release>=(6,2) else 'groups']={'createMissing':True}
    if not api.call('configuration.import',{'format':'json','source':source,'rules':rules}):
        raise ApiError('Zabbix did not confirm the import.')
    found=api.call('template.get',{'filter':{'host':[template['template']]},'output':['templateid','host']})
    if len(found)!=1:raise ApiError('Could not uniquely identify the imported template.')
    templateid=found[0]['templateid']
    items=api.call('item.get',{'hostids':[templateid],'output':['key_','snmp_oid','value_type'],
                              'selectPreprocessing':['type','params']})
    actual={item['key_']:item for item in items}
    types={'FLOAT':'0','CHAR':'1','UNSIGNED':'3','TEXT':'4'}
    for expected in template['items']:
        item=actual.get(expected['key'])
        if not item or item['snmp_oid']!=expected['snmp_oid'] or str(item['value_type'])!=types[expected['value_type']]:
            raise ApiError('Post-import item verification failed: '+expected['key'])
        scale=expected.get('preprocessing',[])
        if scale:
            steps=item.get('preprocessing',[])
            if len(steps)!=1 or str(steps[0]['type'])!='1' or steps[0]['params']!=scale[0]['parameters'][0]:
                raise ApiError('Post-import scaling verification failed: '+expected['key'])
    for method,objects,field in [('trigger.get',export['triggers'],'description'),('graph.get',export['graphs'],'name')]:
        result=api.call(method,{'hostids':[templateid],'output':[field]})
        names={obj[field] for obj in result}
        if not all(obj['name'] in names for obj in objects):raise ApiError('Post-import verification failed: '+method)
    return templateid,len(template['items']),len(export['triggers']),len(export['graphs'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True,help='Zabbix frontend or API URL (HTTPS)')
    parser.add_argument('--username',default='Admin')
    parser.add_argument('--template',type=Path,default=Path(__file__).with_name('ragtech_ups_snmp.json'))
    args=parser.parse_args()
    api=None
    try:
        source=args.template.read_text()
        api=ZabbixApi(args.url)
        version,release=api.version()
        print('Connected to Zabbix '+version)
        password=os.environ.get('ZABBIX_PASSWORD') or getpass.getpass('Zabbix password: ')
        api.token=api.call('user.login',{'username':args.username,'password':password})
        del password
        templateid,items,triggers,graphs=install(api,source,release)
        print(f'Installed and verified Ragtech UPS by SNMP (ID {templateid}): {items} items, {triggers} triggers, {graphs} graphs.')
        print('Link the template to your UPS host and configure its SNMP v2c interface.')
        return 0
    except (ApiError,OSError,ValueError,KeyError) as exc:
        print(str(exc),file=sys.stderr)
        return 1
    finally:
        if api and api.token:
            try:api.call('user.logout',[])
            except ApiError:pass
            api.token=None

if __name__=='__main__':sys.exit(main())
