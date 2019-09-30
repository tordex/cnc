#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/pwm.h>
#include <net/ip.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/math64.h>
#include <linux/workqueue.h>

/*#define PWM_DEBUG*/
#define LOG_PREFIX      "ethpwm: "


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


struct channel_data
{
    uint32_t            seq;
    uint8_t             enable;
    uint16_t            freq;
    uint16_t            scale;
    uint16_t            offset;
    uint16_t            value;
};

struct pwm_work
{
    struct work_struct work;
    struct pwm_state   state;
    struct pwm_device* pwm;
};

struct ethpwm_data
{
    struct pwm_device*  pwm;
    uint8_t             channel;
    uint8_t             init;
    struct channel_data data;
    struct pwm_state    state;
    struct workqueue_struct* wq;
};

/* Function for work */
static void update_pwm_work(struct work_struct *work)
{
    struct pwm_work* data = (struct pwm_work*) work;

    pwm_apply_state(data->pwm, &data->state);

    kfree(work);
}

static inline pwm_packet_t *ethpwm_hdr(const struct sk_buff *skb)
{
	return (pwm_packet_t*) skb_network_header(skb);
}

static inline int cmp_channel(const struct channel_data* d1, const struct channel_data* d2)
{
    if(d1->enable != d2->enable ||
       d1->freq   != d2->freq ||
       d1->scale  != d2->scale ||
       d1->offset != d2->offset ||
       d1->value  != d2->value)
    {
        return 1;
    }
    return 0;
}

static inline void parse_packet(const pwm_packet_t* pkt, struct channel_data* data)
{
    data->seq    = ntohl(pkt->seq);
    data->enable = pkt->enable;
    data->freq   = ntohs(pkt->freq);
    data->scale  = ntohs(pkt->scale);
    data->offset = ntohs(pkt->offset);
    data->value  = ntohs(pkt->value);
}

static inline void update_pwm(struct ethpwm_data* priv)
{
    struct pwm_work* work;
    if(!priv->init)
    {
        priv->init = 1;
    }
    priv->state.period = (unsigned int) div_u64(1000000000L, priv->data.freq);
    priv->state.duty_cycle = priv->state.period * priv->data.value / priv->data.scale;
    if(priv->state.duty_cycle > priv->state.period)
    {
        priv->state.duty_cycle = priv->state.period;
    }
    priv->state.enabled = priv->data.enable;

#ifdef PWM_DEBUG
    printk(KERN_INFO LOG_PREFIX "PWM period: %u, duty_cycle: %u, polarity: %d\n", priv->state.period, priv->state.duty_cycle, priv->state.polarity);
#endif
    
    work = (struct pwm_work*) kmalloc(sizeof(struct pwm_work), GFP_KERNEL);
    if(work)
    {
        INIT_WORK((struct work_struct*) work, update_pwm_work);
        work->state = priv->state;
        work->pwm = priv->pwm;
        queue_work(priv->wq, (struct work_struct*) work);
    }
}

static int ethpwm_rcv(struct sk_buff *skb, struct net_device *dev,
                   struct packet_type *pt, struct net_device *orig_dev)
{
    pwm_packet_t* pkt;
    struct channel_data chd;
    struct ethpwm_data* priv = pt->af_packet_priv;

    if(skb->pkt_type == PACKET_OTHERHOST || skb->pkt_type == PACKET_LOOPBACK)
        goto consumeskb;

    pkt = ethpwm_hdr(skb);
    if(pkt == NULL)
    {
        printk(KERN_ERR LOG_PREFIX "pkt is NULL\n");
        goto freeskb;
    }

#ifdef PWM_DEBUG    
    printk(KERN_INFO LOG_PREFIX "seq: %u, channel: %hhu, enable: %hhu, freq: %hu, value: %hu, scale: %hu, offset: %hu\n",
            ntohl(pkt->seq),
            pkt->channel,
            pkt->enable,
            ntohs(pkt->freq),
            ntohs(pkt->value),
            ntohs(pkt->scale),
            ntohs(pkt->offset));
#endif

    if(pkt->channel != priv->channel)
    {
        goto consumeskb;
    }

    parse_packet(pkt, &chd);

    if(priv->init && priv->data.seq + 1 != chd.seq)
    {
        printk(KERN_ERR LOG_PREFIX "reorder or lost packet last_seq: %u, seq %u\n", 
                priv->data.seq, chd.seq);
    }

    if(!priv->init || cmp_channel(&chd, &priv->data))
    {
        priv->data = chd;
        update_pwm(priv);
    }

consumeskb:
    consume_skb(skb);
    return NET_RX_SUCCESS;

freeskb:
    kfree_skb(skb);
    return NET_RX_DROP;
}


static struct packet_type ethpwm_packet_type __read_mostly = 
{
    .type = cpu_to_be16(0xEAEB),
    .func = ethpwm_rcv,
};


static int ethpwm_probe(struct platform_device *pdev)
{
    struct ethpwm_data* priv = (struct ethpwm_data*) devm_kzalloc(&pdev->dev, sizeof(struct ethpwm_data), GFP_KERNEL);
    
    printk(KERN_INFO LOG_PREFIX "device is probed\n");
    
    if(!priv)
    {
        printk(KERN_ERR LOG_PREFIX  "out off memory\n");
        return -ENOMEM;
    }

    priv->channel = 0;

    priv->pwm = pwm_get(&pdev->dev, NULL);
    if(IS_ERR(priv->pwm))
    {
        printk(KERN_ERR LOG_PREFIX  "PWM device is not found\n");
        return -ENODEV;
    }
    printk(KERN_INFO LOG_PREFIX "PWM device is found\n");

    /* Initialize workqueue */
    priv->wq = alloc_ordered_workqueue("ethpwm%hhu", WQ_HIGHPRI, priv->channel);
    if(!priv->wq)
    {
        printk(KERN_ERR LOG_PREFIX  "Impossible to create workqueue\n");
        return -ENODEV;
    }

    /* Initialize PWM */
    pwm_init_state(priv->pwm, &priv->state);
    priv->state.polarity = PWM_POLARITY_NORMAL;
    priv->state.enabled = 0;
    pwm_apply_state(priv->pwm, &priv->state);

    /* install protocol handler */
    ethpwm_packet_type.af_packet_priv = priv;
    dev_add_pack(&ethpwm_packet_type);

    printk(KERN_INFO LOG_PREFIX "Protocol handler is installed\n");

    /* save driver data */
    platform_set_drvdata(pdev, priv);

    printk(KERN_INFO LOG_PREFIX "device is loaded at channel #%hhu\n", priv->channel);
    
    return 0;   //return 0 for success
}

static int ethpwm_remove(struct platform_device *pdev)
{
    struct ethpwm_data *priv = platform_get_drvdata(pdev);
    if(priv)
    {
        flush_workqueue(priv->wq);
        destroy_workqueue(priv->wq);
        dev_remove_pack(&ethpwm_packet_type);
        if(priv->pwm)
        {
            priv->state.enabled = 0;
            pwm_apply_state(priv->pwm, &priv->state);
            pwm_put(priv->pwm);
        }
    }
    printk(KERN_INFO LOG_PREFIX "device is removed\n");
    return 0;
}

static struct of_device_id ethpwm_match_table[] = 
{
    {
        .compatible = "ethpwm_proto"
    },
    {},
};
MODULE_DEVICE_TABLE(of, ethpwm_match_table);

static struct platform_driver ethpwm_platform_driver = 
{
    .driver = {
        .name = "ethpwm",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(ethpwm_match_table)
    },
    .probe = ethpwm_probe,
    .remove = ethpwm_remove,
};
module_platform_driver(ethpwm_platform_driver);

MODULE_AUTHOR("Yuri Kobets");
MODULE_DESCRIPTION("ETHPWM protocol handler");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
