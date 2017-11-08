
#include "lwip/sys.h" 

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "user_config.h"



#include "apconfig1.h" /* for SSID and SSID_PASSWORD */



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
    struct espconn *conn = arg;
    httpAdm_t * adm = conn->reverse;

    char buf[128];
    
    os_printf( "%s()\n", __FUNCTION__ );
    espconn_regist_recvcb( conn, data_received );

    os_sprintf( buf, "%s %s HTTP/1.1\r\nHost:172.31.42.254\r\nConnection: close\r\n\r\n", 
                         adm->cmd, adm->path );
    
//    os_printf( "Sending: %s\n", buf );
    espconn_send( conn, buf, os_strlen( buf ) );

}


void http_disconnected( void *arg )
{
    struct espconn *conn = arg;
    
    os_printf( "%s\n", __FUNCTION__ );

    os_delay_us(1500000);
//    system_os_post(user_procTaskPrio, 0, 0 );
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


volatile int __cnt__ = 'A';

static void ICACHE_FLASH_ATTR
report(os_event_t *events)
{
    int idx;


    os_printf("PIN 4,5=%d,%d  %c.\n",GPIO_INPUT_GET(4),GPIO_INPUT_GET(5),__cnt__);

    GPIO_OUTPUT_SET(0,~GPIO_INPUT_GET(0));

//    os_printf("GPIOPENDING:%x.\n",gpio_intr_pending());

    os_printf("%s(&{%x,%x}).\n",__FUNCTION__,events->sig,events->par);


    if (events->sig==2)
    {
        if (cnc_ip.addr!=0)
        {
            os_printf("%s: IP=%lX.\n",__FUNCTION__,cnc_ip.addr);

            httpIP(&cnc_ip,"GET", cnc_path,NULL);
        }
        else
        {
            os_printf("%s: Can't do http with IP=%lX.\n",__FUNCTION__,cnc_ip.addr);
        }

    }

}







void dns_done( const char *name, ip_addr_t *ipaddr, void *arg )
{
    
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
        
            wifi_station_connect();

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
gpio_intr_handler(uint32_t iMask, void *arg)
{
    uint32_t gpio_status;
    gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
    __cnt__++;
//    os_printf("\n[%x,%x,%x,%x]",GPIO_REG_READ(0x00),GPIO_REG_READ(0x04),GPIO_REG_READ(0x08),GPIO_REG_READ(0x0C));
//    os_printf("[%x,%x,%x,%x]",GPIO_REG_READ(0x10),GPIO_REG_READ(0x14),GPIO_REG_READ(0x18),gpio_status);
//    os_printf("[%x,%x,%x,%x]",GPIO_REG_READ(0x20),GPIO_REG_READ(0x24),GPIO_REG_READ(0x28),GPIO_REG_READ(0x2C));

    if (gpio_status)
    {
        system_os_post(user_procTaskPrio, 2, gpio_status );
    }

    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);

}


void ICACHE_FLASH_ATTR
sniffer_system_init_done(void)
{
    int stat;
    os_printf("SOF %s().\n",__FUNCTION__);

    GPIO_OUTPUT_SET(0,1);
   
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U,FUNC_GPIO5);
    GPIO_DIS_OUTPUT(GPIO_ID_PIN(5));
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U,FUNC_GPIO4);
    GPIO_DIS_OUTPUT(GPIO_ID_PIN(4));

    ETS_GPIO_INTR_DISABLE();
//    gpio_intr_handler_register(gpio_intr_handler, NULL);
    ETS_GPIO_INTR_ATTACH(gpio_intr_handler, NULL);

    gpio_pin_intr_state_set( GPIO_ID_PIN(5), GPIO_PIN_INTR_POSEDGE);
    gpio_pin_intr_state_set( GPIO_ID_PIN(4), GPIO_PIN_INTR_POSEDGE);

    ETS_GPIO_INTR_ENABLE();


//    gpio_pin_intr_state_set( 1<<5, GPIO_PIN_INTR_POSEDGE);
//    gpio_pin_intr_state_set( 0x18, GPIO_PIN_INTR_ANYEDGE);

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
    
    wifi_station_set_hostname( "button" );
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
    
    system_os_task(report, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);

    system_init_done_cb(sniffer_system_init_done);
    os_printf("EOF %s().\n",__FUNCTION__);
}



