#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "rtapi.h"              /* RTAPI realtime OS API */
#include "rtapi_app.h"          /* RTAPI realtime module decls */
#include "hal.h"                /* HAL public API decls */

/* module information */
MODULE_AUTHOR("Yuri Kobets");
MODULE_DESCRIPTION("PWM/PDM Generator by ethernet for LinuxCNC HAL");
MODULE_LICENSE("GPL");

static int channels = 1;                    /* number of pwm generators configured */
static char* iface = "eth0";                /* interface to use for sending packets */
static char* dst = "ff:ff:ff:ff:ff:ff";     /* MAC address of destination device */

RTAPI_MP_INT(channels, "Number of channels channels");
RTAPI_MP_STRING(iface, "Network interface name");
RTAPI_MP_STRING(dst, "MAC address of destination PWM device");


/***********************************************************************
*                STRUCTURES AND GLOBAL VARIABLES                       *
************************************************************************/

typedef struct 
{
    hal_bit_t *enable;        /* pin for enable signal */
    hal_float_t *value;        /* command value */
    hal_float_t *scale;        /* pin: scaling from value to duty cycle */
    hal_float_t *offset;    /* pin: offset: this is added to duty cycle */
    hal_float_t *pwm_freq;    /* pin: (max) output frequency in Hz */
} ethpwm_t;

typedef struct
{
    int enable;
    double value;
    double scale;
    double offset;
    double pwm_freq;
} ethpwm_old_t;

typedef struct __attribute__((packed))
{
    __be32  seq;
    __u8    channel;
    __u8    enable;
    __be16  value;
    __be16  scale;
    __be16  offset;
    __be16  freq;
} pwm_packet_t;

struct pwm_work
{
    struct work_struct 	work;
	pwm_packet_t 		packet;
};

/* ptr to array of ethpwm_t structs in shared memory, 1 per channel */
static ethpwm_t *ethpwm_array;

/* ptr to array of ethpwm_old_t structs in shared memory, 1 per channel */
static ethpwm_old_t *ethpwm_old;

static unsigned char dst_addr[ETH_ALEN];

/* other globals */
static int comp_id;        /* component ID */
static struct net_device* eth_dev = NULL;
static __u32 seq_num = 0;
/* Workqueue for sending packets */
static struct workqueue_struct* wq;


/***********************************************************************
*                  LOCAL FUNCTION DECLARATIONS                         *
************************************************************************/

static int export_ethpwm(int num, ethpwm_t * addr, ethpwm_old_t* old);
static void update(void *arg, long period);

/***********************************************************************
*                       INIT AND EXIT CODE                             *
************************************************************************/

int rtapi_app_main(void)
{
    int n, retval;

    if (channels <= 0) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "ethpwm: ERROR: no channels configured\n");
        return -1;
    }

    eth_dev = dev_get_by_name(&init_net, iface);
    if(eth_dev == NULL)
    {
        rtapi_print_msg(RTAPI_MSG_ERR, "ethpwm: ERROR: network device is not found\n");
        return -1;
    }
	
    /* Initialize workqueue */
    wq = alloc_ordered_workqueue("ethpwm", WQ_HIGHPRI);
    if(!wq)
    {
        printk(KERN_ERR "Impossible to create workqueue\n");
        return -1;
    }

    if(sscanf(dst, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%*c", &dst_addr[0], &dst_addr[1], &dst_addr[2], &dst_addr[3],
            &dst_addr[4], &dst_addr[5]) != ETH_ALEN)
    {
        rtapi_print_msg(RTAPI_MSG_ERR, "ethpwm: ERROR: incorrect destrination address\n");
        return -1;
    }

    /* have good config info, connect to the HAL */
    comp_id = hal_init("ethpwm");
    if (comp_id < 0) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR, "ethpwm: ERROR: hal_init() failed\n");
        return -1;
    }
    /* allocate shared memory for generator data */
    ethpwm_array = hal_malloc(channels * sizeof(ethpwm_t));
    if (ethpwm_array == 0) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "ethpwm: ERROR: hal_malloc() failed\n");
        hal_exit(comp_id);
        return -1;
    }
    /* allocate shared memory for old data */
    ethpwm_old = hal_malloc(channels * sizeof(ethpwm_old_t));
    if (ethpwm_array == 0) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "ethpwm: ERROR: hal_malloc() failed\n");
        hal_exit(comp_id);
        return -1;
    }
    /* export all the variables for each PWM generator */
    for (n = 0; n < channels; n++) 
    {
        /* export all vars */
        retval = export_ethpwm(n, &(ethpwm_array[n]), &(ethpwm_old[n]));
        if (retval != 0) 
        {
            rtapi_print_msg(RTAPI_MSG_ERR,
                "ethpwm: ERROR: ethpwm %d var export failed\n", n);
            hal_exit(comp_id);
            return -1;
        }
    }
    /* export functions */
    retval = hal_export_funct("ethpwm.update", update,
        ethpwm_array, 1, 0, comp_id);
    if (retval != 0) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "ethpwm: ERROR: update funct export failed\n");
        hal_exit(comp_id);
        return -1;
    }
    rtapi_print_msg(RTAPI_MSG_INFO,
        "ethpwm: installed %d PWM/PDM generators\n", channels);
    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    if(wq)
    {
        flush_workqueue(wq);
        destroy_workqueue(wq);
    }
    if(eth_dev)
    {
        dev_put(eth_dev);
        eth_dev = NULL;
    }
    hal_exit(comp_id);
}

/***********************************************************************
*              REALTIME STEP PULSE GENERATION FUNCTIONS                *
************************************************************************/

static void send_packet_work(struct work_struct *work)
{
    struct pwm_work* data = (struct pwm_work*) work;
    pwm_packet_t* packet;
    struct sk_buff* skb = alloc_skb(ETH_HLEN + sizeof(pwm_packet_t), GFP_ATOMIC);
    if(skb)
    {
        skb->dev = eth_dev;
        skb->pkt_type = PACKET_OUTGOING;
        skb_reserve(skb, ETH_HLEN);
        skb_reset_network_header(skb);

        packet = (pwm_packet_t*) skb_put(skb, sizeof(pwm_packet_t));
        *packet = data->packet;

        if(dev_hard_header(skb, eth_dev, 0xEAEB, dst_addr, eth_dev->dev_addr, eth_dev->addr_len) >= 0)
        {
            dev_queue_xmit(skb);
        } else
        {
            printk(KERN_ERR "ethpwm: error dev_hard_header");
            kfree_skb(skb);
        }
    }
    kfree(work);
}

static void send_packet(int idx)
{
    struct pwm_work* work = (struct pwm_work*) kmalloc(sizeof(struct pwm_work), GFP_KERNEL);
    if(work)
    {
        INIT_WORK((struct work_struct*) work, send_packet_work);

        work->packet.channel = (__u8) idx;
        work->packet.seq     = htonl((seq_num++));
        work->packet.value   = htons((__u16) *(ethpwm_array[idx].value));
        work->packet.scale   = htons((__u16) *ethpwm_array[idx].scale);
        work->packet.offset  = htons((__u16) *ethpwm_array[idx].offset);
        work->packet.freq    = htons((__u16) *ethpwm_array[idx].pwm_freq);
        work->packet.enable  = *ethpwm_array[idx].enable;

        queue_work(wq, (struct work_struct*) work);
    } else
    {
        printk(KERN_ERR "ethpwm: memory allocation for pwm work is failed");
    }
}

static int is_channel_changed(int idx)
{
    if(ethpwm_old[idx].value != *ethpwm_array[idx].value ||
       ethpwm_old[idx].scale != *ethpwm_array[idx].scale ||
       ethpwm_old[idx].offset != *ethpwm_array[idx].offset ||
       ethpwm_old[idx].pwm_freq != *ethpwm_array[idx].pwm_freq ||
       ethpwm_old[idx].enable != *ethpwm_array[idx].enable)
    {
        ethpwm_old[idx].value = *ethpwm_array[idx].value;
        ethpwm_old[idx].scale = *ethpwm_array[idx].scale;
        ethpwm_old[idx].offset = *ethpwm_array[idx].offset;
        ethpwm_old[idx].pwm_freq = *ethpwm_array[idx].pwm_freq;
        ethpwm_old[idx].enable = *ethpwm_array[idx].enable;

        return 1;
    }
    return 0;
}

static void update(void *arg, long period)
{
    int i;
    for(i = 0; i < channels; i++)
    {
        if(is_channel_changed(i))
        {
            send_packet(i);
        }
    }
}


/***********************************************************************
*                   LOCAL FUNCTION DEFINITIONS                         *
************************************************************************/

static int export_ethpwm(int num, ethpwm_t * addr, ethpwm_old_t* old)
{
    int retval, msg;

    /* This function exports a lot of stuff, which results in a lot of
       logging if msg_level is at INFO or ALL. So we save the current value
       of msg_level and restore it later.  If you actually need to log this
       function's actions, change the second line below */
    msg = rtapi_get_msg_level();
    rtapi_set_msg_level(RTAPI_MSG_WARN);

    /* export pins */
    retval = hal_pin_float_newf(HAL_IO, &(addr->scale), comp_id,
        "ethpwm.%d.scale", num);
    if (retval != 0) 
    {
        return retval;
    }

    retval = hal_pin_float_newf(HAL_IO, &(addr->offset), comp_id,
        "ethpwm.%d.offset", num);
    if (retval != 0) 
    {
        return retval;
    }

    retval = hal_pin_float_newf(HAL_IO, &(addr->pwm_freq), comp_id,
        "ethpwm.%d.pwm-freq", num);
    if (retval != 0) 
    {
        return retval;
    }
    retval = hal_pin_bit_newf(HAL_IN, &(addr->enable), comp_id,
        "ethpwm.%d.enable", num);
    if (retval != 0) 
    {
        return retval;
    }
    retval = hal_pin_float_newf(HAL_IN, &(addr->value), comp_id,
        "ethpwm.%d.value", num);
    if (retval != 0) 
    {
        return retval;
    }
    /* set default pin values */
    *(addr->enable) = 0;
    *(addr->value) = 0.0;
    *(addr->scale) = 1.0;
    *(addr->offset) = 0.0;
    *(addr->pwm_freq) = 0;
    /* init old values */
    old->enable = *(addr->enable) + 1;
    old->value = *(addr->value) + 1.0;
    old->scale = *(addr->scale) + 1.0;
    old->offset = *(addr->offset) + 1.0;
    old->pwm_freq = *(addr->pwm_freq) + 1.0;
    /* restore saved message level */
    rtapi_set_msg_level(msg);
    return 0;
}

