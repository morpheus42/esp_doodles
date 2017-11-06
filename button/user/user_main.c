
#include "lwip/sys.h" 

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "user_config.h"
#include "apconfig1.h" 



//#include "driver/uart.h"

//#include "lld.h"


LOCAL os_timer_t report_timer;


typedef struct httpAdm_tag
{
  char * cmd;
  char * path;
  char * cb;
}httpAdm_t;


struct espconn cnc_conn;
ip_addr_t cnc_ip;
esp_tcp cnc_tcp;



//char cnc_host[] = "kernel.org"; // "172.31.42.254";
char cnc_host[] = "172.31.42.254";
char cnc_path[] = "/index.stm";

httpAdm_t g_httpAdm;


char channel=1;

#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];



char * byte2hex(char * cp, unsigned char b)
{
  char c0 = b&15;
  char c1 = b>>4;
  c0+=0x30;
  c1+=0x30;
  if (c0>'9') c0+= ('a'-'9'-1);
  if (c1>'9') c1+= ('a'-'9'-1);
  *cp++=c1;
  *cp++=c0;
  return cp;
}











void user_rf_pre_init( void )
{
    os_printf("%s.%d.\n",__FUNCTION__,__LINE__);
}


void data_received( void *arg, char *pdata, unsigned short len )
{
    struct espconn *conn = arg;
    
    os_printf( "%s(,,len): %s\n", __FUNCTION__, pdata );
    
    espconn_disconnect( conn );
}


void http_connected( void *arg )
{
    int temperature = 55;   // test data
    struct espconn *conn = arg;
    httpAdm_t * adm = conn->reverse;

    char buf[128];
    
    os_printf( "%s()\n", __FUNCTION__ );
    espconn_regist_recvcb( conn, data_received );

    os_sprintf( buf, "%s %s HTTP/1.1\r\nHost:172.31.42.254\r\nConnection: close\r\n\r\n", 
                         adm->cmd, adm->path );
    
    os_printf( "Sending: %s\n", buf );
    espconn_send( conn, buf, os_strlen( buf ) );

}


void http_disconnected( void *arg )
{
    struct espconn *conn = arg;
    
    os_printf( "%s\n", __FUNCTION__ );
    //wifi_station_disconnect();
    os_delay_us(500000);
    system_os_post(user_procTaskPrio, 0, 0 );
}



static void ICACHE_FLASH_ATTR
httpIP(ip_addr_t * ip, char * cmd, char * path, void * cb )
{
        struct espconn *conn = &cnc_conn;
        httpAdm_t * httpAdm = &g_httpAdm; // TODO: how to malloc ?!?!?
        int stat=0;
        
        httpAdm->cmd=cmd;
        httpAdm->path=path;
        httpAdm->cb=cb;

        conn->type = ESPCONN_TCP;
        conn->state = ESPCONN_NONE;
        conn->proto.tcp=&cnc_tcp;
        conn->proto.tcp->local_port = espconn_port();
        conn->proto.tcp->remote_port = 80;
        os_memcpy( conn->proto.tcp->remote_ip, ip, 4 );
        conn->reverse = httpAdm;

        espconn_regist_connectcb( conn, http_connected );
        espconn_regist_disconcb( conn, http_disconnected );
        
        stat = espconn_connect( conn );
        os_printf("%s:connect()=%d.\n",__FUNCTION__,stat);
    
}



static void ICACHE_FLASH_ATTR
report(os_event_t *events)
{
    int idx;

    os_printf("%s(&{%x,%x}).\n",__FUNCTION__,events->sig,events->par);

    os_printf("%s: IP=%lX.\n",__FUNCTION__,cnc_ip.addr);

    if (cnc_ip.addr!=0)
    {


        httpIP(&cnc_ip,"GET", cnc_path,NULL);

    }



//    os_delay_us(1500000);
//    system_os_post(user_procTaskPrio, 0, 0 );
}







void dns_done( const char *name, ip_addr_t *ipaddr, void *arg )
{
//    struct espconn *conn = arg;
    
    os_printf( "SOF:%s()\n", __FUNCTION__ );
    
    if ( ipaddr == NULL) 
    {
        os_printf("DNS lookup failed\n");
        wifi_station_disconnect();
    }
    else
    {
        os_printf( "%s(): %s =@" IPSTR ".\n", __FUNCTION__, name, IP2STR(ipaddr) );
        system_os_post(user_procTaskPrio, 1, 1 );
    }
    os_printf( "EOF:%s()\n", __FUNCTION__ );
}


void wifi_callback( System_Event_t *evt )
{
    os_printf( "SOF: %s(): %d\n", __FUNCTION__, evt->event );
    
    switch ( evt->event )
    {
        case EVENT_STAMODE_CONNECTED:
        {
            os_printf("connect to ssid %s, channel %d\n",
                        evt->event_info.connected.ssid,
                        evt->event_info.connected.channel);
            break;
        }

        case EVENT_STAMODE_DISCONNECTED:
        {
            os_printf("disconnect from ssid %s, reason %d\n",
                        evt->event_info.disconnected.ssid,
                        evt->event_info.disconnected.reason);
            
            //deep_sleep_set_option( 0 );
            //system_deep_sleep( 60 * 1000 * 1000 );  // 60 seconds
            break;
        }

        case EVENT_STAMODE_GOT_IP:
        {
            int stat=0;
            os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                        IP2STR(&evt->event_info.got_ip.ip),
                        IP2STR(&evt->event_info.got_ip.mask),
                        IP2STR(&evt->event_info.got_ip.gw));
            os_printf("\n");
            
            stat = espconn_gethostbyname( &cnc_conn, cnc_host, &cnc_ip, dns_done );

            if (stat==ESPCONN_OK)
            {
                os_printf("%s: No delay dnsreq: %s IP=" IPSTR ".\n",__FUNCTION__, cnc_host,IP2STR(&cnc_ip));
                system_os_post(user_procTaskPrio, 1, 0 );
            }
            else if (stat==ESPCONN_INPROGRESS)
            {
                os_printf("%s: getting IP for %s.\n",__FUNCTION__, cnc_host);
            }
            else
            {
                os_printf("%s: error in getting IP for %s.\n",__FUNCTION__, cnc_host);
            }


            break;
        }
        
        default:
        {
            break;
        }
    }
    os_printf( "EOF: %s().\n", __FUNCTION__);
}





void ICACHE_FLASH_ATTR
sniffer_system_init_done(void)
{
    int stat;
    os_printf("SOF %s().\n",__FUNCTION__);
    // Promiscuous works only with station mode

//    stat = wifi_set_channel(channel);
//    os_printf("%s.%d: %d\n",__FUNCTION__,__LINE__, stat );

//    wifi_promiscuous_enable(0);

    // Set up promiscuous callback
//    wifi_set_promiscuous_rx_cb(promisc_cb);

//    wifi_promiscuous_enable(1);

    os_printf("EOF %s().\n",__FUNCTION__);
}


void ICACHE_FLASH_ATTR
user_init()
{
    char ssid[32] = SSID;
    char password[64] = SSID_PASSWORD;
    struct station_config stationConf;

    //uart_init(115200, 115200);
    uart_div_modify( 0, UART_CLK_FREQ / ( 115200 ) );
    os_printf("\n\nSDK version:%s\n", system_get_sdk_version());
    
    // Promiscuous works only with station mode

    wifi_station_set_hostname( "esper1" );
    wifi_set_opmode(STATION_MODE);

    gpio_init();

    stationConf.bssid_set = 0;
    os_memcpy(&stationConf.ssid, ssid, 32);
    os_memcpy(&stationConf.password, password, 64);
    //os_printf("%s.%d.\n",__FUNCTION__,__LINE__ );
    wifi_station_set_config(&stationConf); 
    //os_printf("%s.%d.\n",__FUNCTION__,__LINE__ );
    wifi_set_event_handler_cb( wifi_callback );
    wifi_station_dhcpc_start();

    wifi_station_connect();
    
    // Set timer for deauth
//    os_timer_disarm(&deauth_timer);
    //os_timer_setfn(&deauth_timer, (os_timer_func_t *) deauth, NULL);
//    os_timer_arm(&deauth_timer, CHANNEL_HOP_INTERVAL, 1);

//    os_timer_disarm(&report_timer);
//    os_timer_setfn(&report_timer, (os_timer_func_t *)report, NULL);
//    os_timer_arm(&report_timer, 1000, 0);


    system_os_task(report, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
//    system_os_post(user_procTaskPrio, 0, 0 );

    // Continue to 'sniffer_system_init_done'
    system_init_done_cb(sniffer_system_init_done);
    os_printf("EOF %s().\n",__FUNCTION__);
}



