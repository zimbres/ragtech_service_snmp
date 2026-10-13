#define _POSIX_C_SOURCE 200809L
#include "ups_snmp.h"
#include "ups.h"
#include "log.h"
#include "platform.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

/* Bounded BER codec; no allocations, threads, or third-party libraries. */
#define MAX_PACKET 8192
#define MAX_VARS 64
#define MAX_OID 32
typedef struct { const uint8_t *p; size_t n; } reader;
typedef struct { uint8_t p[MAX_PACKET]; size_t n; bool bad; } buffer;
typedef struct { uint32_t a[MAX_OID]; size_t n; } oid;
typedef struct { oid name; int tag; int32_t value; const uint8_t *str; size_t len; } object;
static platform_socket_t sock = PLATFORM_SOCKET_INVALID;
static bool network_started;
static config_t config;
static bool take(reader *r, int *tag, reader *v) {
    if (r->n < 2) return false;
    *tag = *r->p++; r->n--;
    size_t n = *r->p++; r->n--;
    if (n & 128) {
        size_t count = n & 127; n = 0;
        if (!count || count > 2 || count > r->n) return false;
        while (count--) { n = (n << 8) | *r->p++; r->n--; }
    }
    if (n > r->n) return false;
    *v = (reader){r->p,n}; r->p += n; r->n -= n;
    return true;
}
static bool integer(reader *r, int32_t *out) {
    int tag; reader v;
    if (!take(r,&tag,&v) || tag != 2 || !v.n || v.n > 4) return false;
    uint32_t n = (v.p[0] & 128) ? UINT32_MAX : 0;
    while (v.n--) n = (n << 8) | *v.p++;
    *out = (int32_t)n; return true;
}
static bool decode_oid(reader v, oid *o) {
    o->n = 0;
    while (v.n) {
        uint32_t n = 0; unsigned count = 0; uint8_t c;
        do {
            if (!v.n || ++count > 5 || n > (UINT32_MAX >> 7)) return false;
            c = *v.p++; v.n--; n = (n << 7) | (c & 127);
        } while (c & 128);
        if (!o->n) {
            o->a[o->n++] = n < 40 ? 0 : n < 80 ? 1 : 2;
            o->a[o->n++] = n - 40 * o->a[0];
        } else {
            if (o->n == MAX_OID) return false;
            o->a[o->n++] = n;
        }
    }
    return o->n >= 2;
}
static void bytes(buffer *b, const void *p, size_t n) {
    if (n > sizeof(b->p)-b->n) { b->bad = true; return; }
    if (n) memcpy(b->p+b->n,p,n);
    b->n += n;
}
static void tlv(buffer *b, int tag, const void *p, size_t n) {
    uint8_t h[4] = {(uint8_t)tag,0,0,0}; size_t hn;
    if (n < 128) { h[1] = (uint8_t)n; hn = 2; }
    else { h[1] = 0x82; h[2] = (uint8_t)(n >> 8); h[3] = (uint8_t)n; hn = 4; }
    bytes(b,h,hn); bytes(b,p,n);
}
static void number(buffer *b, int32_t n) {
    uint32_t u = (uint32_t)n;
    uint8_t p[4] = {u>>24,u>>16,u>>8,u}; size_t start = 0;
    while (start < 3 && ((p[start]==0 && !(p[start+1]&128)) ||
                            (p[start]==255 && (p[start+1]&128)))) start++;
    tlv(b,2,p+start,4-start);
}
static void arc(buffer *b, uint32_t n) {
    uint8_t p[5]; size_t i = 5;
    p[--i] = n & 127;
    while ((n >>= 7)) p[--i] = (n & 127) | 128;
    bytes(b,p+i,5-i);
}
static void encode_oid(buffer *b, const oid *o) {
    buffer v = {0}; arc(&v,o->a[0]*40+o->a[1]);
    for (size_t i=2;i<o->n;i++) arc(&v,o->a[i]);
    tlv(b,6,v.p,v.n);
}
static int compare(const oid *a, const oid *b) {
    size_t n = a->n < b->n ? a->n : b->n;
    for (size_t i=0;i<n;i++) if (a->a[i]!=b->a[i]) return a->a[i]<b->a[i] ? -1 : 1;
    return a->n == b->n ? 0 : a->n < b->n ? -1 : 1;
}
static int sort_object(const void *a,const void *b) { return compare(&((const object *)a)->name,&((const object *)b)->name); }
static int32_t scaled(float f, float scale) {
    double n = round((double)f * scale);
    if (!isfinite(n)) return 0;
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : (int32_t)n;
}
static void add(object *objects, size_t *count, const char *name, int32_t value,
                const void *str, size_t len) {
    object *o = &objects[(*count)++]; memset(o,0,sizeof(*o));
    while (*name) {
        char *end; o->name.a[o->name.n++] = (uint32_t)strtoul(name,&end,10);
        name = *end ? end+1 : end;
    }
    o->tag = str ? 4 : 2; o->value = value; o->str = str; o->len = len;
}
#define RFC(s,v) add(objects,&n,"1.3.6.1.2.1.33.1." s,v,NULL,0)
static size_t mib(object *objects, ups_snapshot_t *s) {
    size_t n=0; ups_get_snapshot(s);
    ups_metrics_t *m=&s->metrics; ups_status_t *f=&s->status;
    bool battery = f->op_battery || (m->voltage_in < 50 && m->power_out > 10);
    add(objects,&n,"1.3.6.1.2.1.33.1.1.1.0",0,"Ragtech",7);
    add(objects,&n,"1.3.6.1.2.1.33.1.1.2.0",0,s->model,strlen(s->model));
    static const char agent_version[]="ragtech_service 3.0.0";
    add(objects,&n,"1.3.6.1.2.1.33.1.1.4.0",0,agent_version,sizeof(agent_version)-1);
    RFC("2.1.0",!s->connected ? 1 : f->no_battery || f->fail_end_battery ? 4 : f->lo_battery ? 3 : 2);
    RFC("2.2.0",s->seconds_on_battery);
    RFC("2.3.0",m->battery_runtime > 0 && m->battery_runtime < 1 ? 1 : scaled(m->battery_runtime,1));
    int charge=scaled(m->battery_state,1); if(charge<0)charge=0; if(charge>100)charge=100;
    RFC("2.4.0",charge); RFC("2.5.0",scaled(m->battery_voltage,10));
    RFC("3.2.0",1); RFC("3.3.1.3.1",scaled(m->voltage_in,1));
    RFC("4.1.0",!s->connected ? 1 : m->voltage_out < 1 ? 2 : battery ? 5 : 3);
    RFC("4.2.0",scaled(m->frequency,10)); RFC("4.3.0",1);
    RFC("4.4.1.2.1",scaled(m->voltage_out,1));
    RFC("4.4.1.3.1",scaled(m->current_out,10));
    RFC("4.4.1.4.1",scaled(m->power_out,1));
    RFC("4.4.1.5.1",scaled(m->power_out_percent,1));
    float metrics[]={m->power_out_percent,m->current_out,m->voltage_out,m->voltage_in,
        m->power_out,m->power_in,m->energy_out,m->energy_in,m->temperature,
        m->battery_state,m->battery_voltage,m->frequency,m->battery_runtime};
    bool flags[]={f->no_battery,f->old_battery,f->op_checkup,f->no_v_input,f->lo_v_input,
        f->hi_v_input,f->op_battery,f->hi_p_output,f->lo_battery,f->fail_overtemp,
        f->fail_end_battery,f->fail_overload,f->fail_abnormal_vout,f->fail_abnormal_vbat,
        f->fail_inverter,f->fail_shortcircuit,f->sync_input,f->more_battery,
        f->less_battery,f->remote_control_active};
    char name[80];
    for(size_t i=0;i<13;i++) {
        snprintf(name,sizeof(name),"1.3.6.1.3.1628.1.1.%zu.0",i+1);
        add(objects,&n,name,scaled(metrics[i],100),NULL,0);
    }
    for(size_t i=0;i<20;i++) {
        snprintf(name,sizeof(name),"1.3.6.1.3.1628.1.2.%zu.0",i+1);
        add(objects,&n,name,flags[i],NULL,0);
    }
    if (!isfinite(m->frequency)) {
        for (size_t i=0;i<n;i++) {
            oid *o=&objects[i].name;
            bool standard=o->n==11 && o->a[4]==2 && o->a[8]==4 && o->a[9]==2;
            bool extension=o->n==10 && o->a[4]==3 && o->a[7]==1 && o->a[8]==12;
            if (standard || extension) {
                objects[i].tag=0x81;objects[i].str=NULL;objects[i].len=0;
            }
        }
    }
    add(objects,&n,"1.3.6.1.3.1628.1.3.0",s->connected,NULL,0);
    add(objects,&n,"1.3.6.1.3.1628.1.4.0",0,s->raw,s->raw_len);
    /* Keep object instances discoverable, but never represent stale readings as live data. */
    for (size_t i=0;i<n;i++) {
        oid *o=&objects[i].name;
        bool rfc=o->n>8 && o->a[4]==2;
        if (rfc) {
            unsigned group=o->a[8], item=o->a[9];
            bool reading=(group==2 && item>=3) || (group==3 && item==3) ||
                         (group==4 && (item==2 || item==4));
            if ((!s->connected && reading) ||
                (group==2 && item==3 && m->battery_runtime<=0)) {
                objects[i].tag=0x81; objects[i].str=NULL; objects[i].len=0;
            }
        }
    }
    qsort(objects,n,sizeof(*objects),sort_object); return n;
}
static void varbind(buffer *list, const oid *name, const object *o, int exception) {
    buffer v={0}; encode_oid(&v,name);
    if (!o) tlv(&v,exception,NULL,0);
    else if(o->tag==2) number(&v,o->value);
    else tlv(&v,o->tag,o->str,o->len);
    tlv(list,0x30,v.p,v.n); list->bad |= v.bad;
}
size_t snmp_response(const config_t *cfg,const uint8_t *request,size_t length,uint8_t *response,size_t capacity) {
    reader r={request,length}, msg,pdu,list,v,community; int tag,kind;
    int32_t version,id,first,second;
    if(length>MAX_PACKET || !take(&r,&tag,&msg) || tag!=0x30 || r.n ||
       !integer(&msg,&version) || version!=1 || !take(&msg,&tag,&community) || tag!=4 ||
       community.n!=strlen(cfg->snmp_community) || memcmp(community.p,cfg->snmp_community,community.n) ||
       !take(&msg,&kind,&pdu) || msg.n || (kind!=0xa0 && kind!=0xa1 && kind!=0xa5 && kind!=0xa3) ||
       !integer(&pdu,&id) || !integer(&pdu,&first) || !integer(&pdu,&second) ||
       !take(&pdu,&tag,&list) || tag!=0x30 || pdu.n) return 0;
    oid names[MAX_VARS]; size_t count=0;
    while(list.n) {
        reader entry,ignored;
        if(count==MAX_VARS || !take(&list,&tag,&entry) || tag!=0x30 ||
           !take(&entry,&tag,&v) || tag!=6 || !decode_oid(v,&names[count]) ||
           !take(&entry,&tag,&ignored) || entry.n) return 0;
        count++;
    }
    object objects[MAX_VARS]; ups_snapshot_t snapshot; size_t total=mib(objects,&snapshot);
    buffer result={0}; int error=0,index=0;
    if(kind==0xa3) {
        error=count ? 17 : 0; index=count ? 1 : 0;
        /* SET is read-only; preserve the request varbind values. */
        reader again={request,length},tmp; take(&again,&tag,&tmp);
        integer(&tmp,&version);take(&tmp,&tag,&v);take(&tmp,&tag,&pdu);
        integer(&pdu,&id);integer(&pdu,&first);integer(&pdu,&second);take(&pdu,&tag,&v);
        bytes(&result,v.p,v.n);
    } else {
        size_t nonrepeat=kind==0xa5 ? (first<0?0:(size_t)first) : count;
        if(nonrepeat>count)nonrepeat=count;
        size_t repeat=kind==0xa5 ? (second<0?0:(size_t)second) : 0;
        if(repeat>MAX_VARS)repeat=MAX_VARS;
        size_t emitted=0;
        for(size_t round=0;round<=repeat;round++) {
            size_t begin=round ? nonrepeat : 0, end=round ? count : nonrepeat;
            bool all_end=true;
            for(size_t i=begin;i<end;i++) {
                const object *found=NULL;
                for(size_t j=0;j<total;j++) {
                    int cmp=compare(&objects[j].name,&names[i]);
                    if((kind==0xa0 && cmp==0) || (kind!=0xa0 && cmp>0)) {found=&objects[j];break;}
                }
                if(++emitted>MAX_VARS)goto done;
                if(found) {names[i]=found->name;all_end=false;}
                int exception=0x82;
                if(kind==0xa0) {
                    exception=0x80;
                    for(size_t j=0;j<total;j++) {
                        oid base=objects[j].name; base.n--;
                        if(names[i].n>=base.n && !memcmp(names[i].a,base.a,base.n*sizeof(uint32_t))) {exception=0x81;break;}
                    }
                }
                varbind(&result,&names[i],found,exception);
            }
            if(round && all_end)break;
        }
    }
done:;
    buffer bindings={0},body={0},outer={0},packet={0};
    tlv(&bindings,0x30,result.p,result.n);
    number(&body,id);number(&body,error);number(&body,index);bytes(&body,bindings.p,bindings.n);
    number(&outer,1);tlv(&outer,4,community.p,community.n);tlv(&outer,0xa2,body.p,body.n);
    tlv(&packet,0x30,outer.p,outer.n);
    if(result.bad || bindings.bad || body.bad || outer.bad || packet.bad || packet.n>capacity) {
        body=(buffer){0};outer=(buffer){0};packet=(buffer){0};
        number(&body,id);number(&body,1);number(&body,0);tlv(&body,0x30,NULL,0);
        number(&outer,1);tlv(&outer,4,community.p,community.n);tlv(&outer,0xa2,body.p,body.n);
        tlv(&packet,0x30,outer.p,outer.n);
        if(packet.bad || packet.n>capacity)return 0;
    }
    memcpy(response,packet.p,packet.n);return packet.n;
}
int snmp_init(const config_t *cfg) {
    snmp_cleanup();config=*cfg;
    if(!cfg->snmp_community[0] || cfg->snmp_port<1 || cfg->snmp_port>65535 ||
       !platform_ipv4_valid(cfg->snmp_bind_address)) return -1;
    if(platform_network_init()<0)return -1;
    network_started=true;
    sock=platform_udp_open(cfg->snmp_bind_address,cfg->snmp_port);
    if(sock==PLATFORM_SOCKET_INVALID) {
        LOG_ERROR("Cannot bind SNMP %s:%d: %s",cfg->snmp_bind_address,cfg->snmp_port,platform_error());
        snmp_cleanup();return -1;
    }
    LOG_INFO("SNMP v2c listening on %s:%d",cfg->snmp_bind_address,cfg->snmp_port);
    return 0;
}
void snmp_poll(int timeout_ms) {
    uint8_t request[MAX_PACKET+1],response[MAX_PACKET];struct sockaddr_in peer;
    int n=platform_udp_receive(sock,request,sizeof(request),timeout_ms,&peer);
    if(n<=0)return;
    size_t size=snmp_response(&config,request,(size_t)n,response,sizeof(response));
    if(size)platform_udp_send(sock,response,size,&peer);
}
void snmp_cleanup(void) {
    if(sock!=PLATFORM_SOCKET_INVALID)platform_udp_close(sock);
    sock=PLATFORM_SOCKET_INVALID;
    if(network_started)platform_network_cleanup();
    network_started=false;
}
