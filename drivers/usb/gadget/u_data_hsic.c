/* Copyright (c) 2011-2013, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/termios.h>
#include <mach/usb_bridge.h>
#include <mach/usb_gadget_xport.h>

#ifdef CONFIG_PANTECH_PDL_DLOAD
// 20120514 na_sungju
#include <linux/reboot.h>
#include <linux/workqueue.h>
#include "sky_rawdata.h"
#include "u_serial.h"

#if (0) //	F_PANTECH_SECBOOT
//#include "../../../../../../../boot_images/core/securemsm/secboot/shared/inc/secboot_types.h"
typedef struct
{ 
  #define SECBOOT_FUSE_FLAG_MAGIC_NUM     0xAAFFFF
  #define SECBOOT_FUSE_FLAG_UNSET         0xF0F0F0
  #define SECBOOT_FUSE_FLAG_SET           0xF1F1F1
  #define SECBOOT_FUSE_NOT_BLOWN          0xF2F2F2
  #define SECBOOT_FUSE_BLOWN              0xF3F3F3  

  unsigned int secboot_magic_num;

  unsigned int auth_en;
  unsigned int shk_blow;
  unsigned int shk_rw_dis;
  unsigned int jtag_dis;
} secboot_fuse_flag;
/*
typedef struct
{ 
  #define SECBOOT_SECBOOT_CHECK_MAGIC_NUM     0xCCBBBB
  #define SECBOOT_AUTH_IMG_NUM                16

  uint32 secboot_magic_num;
  uint32 auth_img_result[SECBOOT_AUTH_IMG_NUM];
} secboot_auth_img_result;
*/
#endif



enum {
  DLOADINFO_NONE_STATE = 0,
  DLOADINFO_AT_RESPONSE_STATE,
  DLOADINFO_PHONE_INFO_STATE,
  DLOADINFO_HASH_TABLE_STATE,
  DLOADINFO_PARTI_TABLE_STATE,
  DLOADINFO_FINISH_STATE,
  DLOADINFO_MAX_STATE
};

enum {
  FINISH_CMD = 0,
  PHONE_INFO_CMD,
  HASH_TABLE_CMD,
  PARTI_TABLE_CMD,
  MAX_PHONEINFO_CMD
};

typedef struct {
  unsigned int partition_size_;
  char   partition_name_[ 8 ];
}partition_info_type;

typedef  unsigned long int  uint32;      /* Unsigned 32 bit value */
typedef  unsigned char      uint8;       /* Unsigned 8  bit value */

typedef struct {
  uint32 version_;
  char   model_name_    [ 16 ];
  char   binary_version_[ 16 ];
  uint32 fs_version_;
  uint32 nv_version_;
  char   build_date_    [ 16 ];
  char   build_time_    [ 16 ];


  uint32 boot_loader_version_;                
  uint32 boot_section_id_[4];                 
  

  uint32              efs_size_;                
  uint32              partition_num_;           
  partition_info_type partition_info_[ 6 ];     

  uint32 FusionID;
  uint8  Imei[15];

  /* F_PANTECH_SECBOOT - DO NOT erase this comment! */
  char   secure_magic_[ 7 ];
  // 20130423 p14694 jmkim Add field information
  char sub_binary_version_[ 8 ]; 
  uint32 preload_checksum;
  uint8  reserved_2[ 42 ];
  
} __attribute__((packed)) phoneinfo_type;

static struct delayed_work phoneinfo_read_wqst;
static char pantech_phoneinfo_buff[SECTOR_SIZE]={0,};
static char temp_buf[SECTOR_SIZE]={0,};
#define NV_UE_IMEI_SIZE             9
#define IMEI_ADDR_MAGIC_NUM         0x88776655

typedef struct
{
  uint32 imei_magic_num;
  uint8 backup_imei[NV_UE_IMEI_SIZE];
  uint8 emptspace[51];
} imei_backup_info_type;

static void load_phoneinfo_with_imei(struct work_struct *work_s);
static unsigned fill_writereq(int *dloadinfo_state, struct usb_request *writereq);
static unsigned int fill_phoneinfo(char *buff);
static unsigned int check_phoneinfo(void);
static int dloadinfo_state = DLOADINFO_NONE_STATE;
#endif
static unsigned int no_data_ports;

#define GHSIC_DATA_RMNET_RX_Q_SIZE		50
#define GHSIC_DATA_RMNET_TX_Q_SIZE		300
#define GHSIC_DATA_SERIAL_RX_Q_SIZE		10
#define GHSIC_DATA_SERIAL_TX_Q_SIZE		20
#define GHSIC_DATA_RX_REQ_SIZE			2048
#define GHSIC_DATA_TX_INTR_THRESHOLD		20

static unsigned int ghsic_data_rmnet_tx_q_size = GHSIC_DATA_RMNET_TX_Q_SIZE;
module_param(ghsic_data_rmnet_tx_q_size, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_rmnet_rx_q_size = GHSIC_DATA_RMNET_RX_Q_SIZE;
module_param(ghsic_data_rmnet_rx_q_size, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_serial_tx_q_size = GHSIC_DATA_SERIAL_TX_Q_SIZE;
module_param(ghsic_data_serial_tx_q_size, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_serial_rx_q_size = GHSIC_DATA_SERIAL_RX_Q_SIZE;
module_param(ghsic_data_serial_rx_q_size, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_rx_req_size = GHSIC_DATA_RX_REQ_SIZE;
module_param(ghsic_data_rx_req_size, uint, S_IRUGO | S_IWUSR);

unsigned int ghsic_data_tx_intr_thld = GHSIC_DATA_TX_INTR_THRESHOLD;
module_param(ghsic_data_tx_intr_thld, uint, S_IRUGO | S_IWUSR);

/*flow ctrl*/
#define GHSIC_DATA_FLOW_CTRL_EN_THRESHOLD	500
#define GHSIC_DATA_FLOW_CTRL_DISABLE		300
#define GHSIC_DATA_FLOW_CTRL_SUPPORT		1
#define GHSIC_DATA_PENDLIMIT_WITH_BRIDGE	500

static unsigned int ghsic_data_fctrl_support = GHSIC_DATA_FLOW_CTRL_SUPPORT;
module_param(ghsic_data_fctrl_support, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_fctrl_en_thld =
		GHSIC_DATA_FLOW_CTRL_EN_THRESHOLD;
module_param(ghsic_data_fctrl_en_thld, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_fctrl_dis_thld = GHSIC_DATA_FLOW_CTRL_DISABLE;
module_param(ghsic_data_fctrl_dis_thld, uint, S_IRUGO | S_IWUSR);

static unsigned int ghsic_data_pend_limit_with_bridge =
		GHSIC_DATA_PENDLIMIT_WITH_BRIDGE;
module_param(ghsic_data_pend_limit_with_bridge, uint, S_IRUGO | S_IWUSR);

#define CH_OPENED 0
#define CH_READY 1

struct gdata_port {
	/* port */
	unsigned		port_num;

	/* gadget */
	atomic_t		connected;
	struct usb_ep		*in;
	struct usb_ep		*out;

	enum gadget_type	gtype;

	/* data transfer queues */
	unsigned int		tx_q_size;
	struct list_head	tx_idle;
	struct sk_buff_head	tx_skb_q;
	spinlock_t		tx_lock;

	unsigned int		rx_q_size;
	struct list_head	rx_idle;
	struct sk_buff_head	rx_skb_q;
	spinlock_t		rx_lock;

	/* work */
	struct workqueue_struct	*wq;
	struct work_struct	connect_w;
	struct work_struct	disconnect_w;
	struct work_struct	write_tomdm_w;
	struct work_struct	write_tohost_w;

	struct bridge		brdg;

	/*bridge status*/
	unsigned long		bridge_sts;

	unsigned int		n_tx_req_queued;

	/*counters*/
	unsigned long		to_modem;
	unsigned long		to_host;
	unsigned int		rx_throttled_cnt;
	unsigned int		rx_unthrottled_cnt;
	unsigned int		tx_throttled_cnt;
	unsigned int		tx_unthrottled_cnt;
	unsigned int		tomodem_drp_cnt;
	unsigned int		unthrottled_pnd_skbs;
};

static struct {
	struct gdata_port	*port;
	struct platform_driver	pdrv;
	char			port_name[BRIDGE_NAME_MAX_LEN];
} gdata_ports[NUM_PORTS];

static unsigned int get_timestamp(void);
static void dbg_timestamp(char *, struct sk_buff *);
static void ghsic_data_start_rx(struct gdata_port *port);

static void ghsic_data_free_requests(struct usb_ep *ep, struct list_head *head)
{
	struct usb_request	*req;

	while (!list_empty(head)) {
		req = list_entry(head->next, struct usb_request, list);
		list_del(&req->list);
		usb_ep_free_request(ep, req);
	}
}

static int ghsic_data_alloc_requests(struct usb_ep *ep, struct list_head *head,
		int num,
		void (*cb)(struct usb_ep *ep, struct usb_request *),
		spinlock_t *lock)
{
	int			i;
	struct usb_request	*req;
	unsigned long		flags;

	pr_debug("%s: ep:%s head:%p num:%d cb:%p", __func__,
			ep->name, head, num, cb);

	for (i = 0; i < num; i++) {
		req = usb_ep_alloc_request(ep, GFP_KERNEL);
		if (!req) {
			pr_debug("%s: req allocated:%d\n", __func__, i);
			return list_empty(head) ? -ENOMEM : 0;
		}
		req->complete = cb;
		spin_lock_irqsave(lock, flags);
		list_add(&req->list, head);
		spin_unlock_irqrestore(lock, flags);
	}

	return 0;
}

static void ghsic_data_unthrottle_tx(void *ctx)
{
	struct gdata_port	*port = ctx;
	unsigned long		flags;

	if (!port || !atomic_read(&port->connected))
		return;

	spin_lock_irqsave(&port->rx_lock, flags);
	port->tx_unthrottled_cnt++;
	spin_unlock_irqrestore(&port->rx_lock, flags);

	queue_work(port->wq, &port->write_tomdm_w);
	pr_debug("%s: port num =%d unthrottled\n", __func__,
		port->port_num);
}

static void ghsic_data_write_tohost(struct work_struct *w)
{
	unsigned long		flags;
	struct sk_buff		*skb;
	int			ret;
	struct usb_request	*req;
	struct usb_ep		*ep;
	struct gdata_port	*port;
	struct timestamp_info	*info;

	port = container_of(w, struct gdata_port, write_tohost_w);

	if (!port)
		return;

	spin_lock_irqsave(&port->tx_lock, flags);
	ep = port->in;
	if (!ep) {
		spin_unlock_irqrestore(&port->tx_lock, flags);
		return;
	}

	while (!list_empty(&port->tx_idle)) {
		skb = __skb_dequeue(&port->tx_skb_q);
		if (!skb)
			break;

		req = list_first_entry(&port->tx_idle, struct usb_request,
				list);
		req->context = skb;
		req->buf = skb->data;
		req->length = skb->len;

		port->n_tx_req_queued++;
		if (port->n_tx_req_queued == ghsic_data_tx_intr_thld) {
			req->no_interrupt = 0;
			port->n_tx_req_queued = 0;
		} else {
			req->no_interrupt = 1;
		}

		list_del(&req->list);

		info = (struct timestamp_info *)skb->cb;
		info->tx_queued = get_timestamp();
		spin_unlock_irqrestore(&port->tx_lock, flags);
		ret = usb_ep_queue(ep, req, GFP_KERNEL);
		spin_lock_irqsave(&port->tx_lock, flags);
		if (ret) {
			pr_err("%s: usb epIn failed\n", __func__);
			list_add(&req->list, &port->tx_idle);
			dev_kfree_skb_any(skb);
			break;
		}
		port->to_host++;
		if (ghsic_data_fctrl_support &&
			port->tx_skb_q.qlen <= ghsic_data_fctrl_dis_thld &&
			test_and_clear_bit(RX_THROTTLED, &port->brdg.flags)) {
			port->rx_unthrottled_cnt++;
			port->unthrottled_pnd_skbs = port->tx_skb_q.qlen;
			pr_debug_ratelimited("%s: disable flow ctrl:"
					" tx skbq len: %u\n",
					__func__, port->tx_skb_q.qlen);
			data_bridge_unthrottle_rx(port->brdg.ch_id);
		}
	}
	spin_unlock_irqrestore(&port->tx_lock, flags);
}

static int ghsic_data_receive(void *p, void *data, size_t len)
{
	struct gdata_port	*port = p;
	unsigned long		flags;
	struct sk_buff		*skb = data;

	if (!port || !atomic_read(&port->connected)) {
		dev_kfree_skb_any(skb);
		return -ENOTCONN;
	}

	pr_debug("%s: p:%p#%d skb_len:%d\n", __func__,
			port, port->port_num, skb->len);

	spin_lock_irqsave(&port->tx_lock, flags);
	__skb_queue_tail(&port->tx_skb_q, skb);

	if (ghsic_data_fctrl_support &&
			port->tx_skb_q.qlen >= ghsic_data_fctrl_en_thld) {
		set_bit(RX_THROTTLED, &port->brdg.flags);
		port->rx_throttled_cnt++;
		pr_debug_ratelimited("%s: flow ctrl enabled: tx skbq len: %u\n",
					__func__, port->tx_skb_q.qlen);
		spin_unlock_irqrestore(&port->tx_lock, flags);
		queue_work(port->wq, &port->write_tohost_w);
		return -EBUSY;
	}

	spin_unlock_irqrestore(&port->tx_lock, flags);

	queue_work(port->wq, &port->write_tohost_w);

	return 0;
}

#ifdef CONFIG_PANTECH_PDL_DLOAD
static unsigned int fill_phoneinfo(char *buff)
{
	phoneinfo_type *pantech_phoneinfo_buff_ptr;

	pantech_phoneinfo_buff_ptr = (phoneinfo_type *)&pantech_phoneinfo_buff[16];
	printk(KERN_ERR "%s: %d\n",__func__,__LINE__);

	if(pantech_phoneinfo_buff_ptr->version_== 0){
		printk(KERN_ERR "%s: phoneinfo is broken or empty\n", __func__);
		return 0;
	}
	memcpy(buff, pantech_phoneinfo_buff, 16 + sizeof(phoneinfo_type));
	printk(KERN_INFO "%s: phoneinfo is OK\n", __func__);  
	return (16 + sizeof(phoneinfo_type));
}

static unsigned int check_phoneinfo(void)
{
	phoneinfo_type *pantech_phoneinfo_buff_ptr;

	pantech_phoneinfo_buff_ptr = (phoneinfo_type *)&pantech_phoneinfo_buff[16];

	if(pantech_phoneinfo_buff_ptr->version_== 0){
		// 20120314, albatros, packet communication performance..
		printk(KERN_ERR "%s: phoneinfo is broken or empty\n", __func__);
		return 0;
	}

	// 20120314, albatros, packet communication performance..
	//printk(KERN_INFO "%s: phoneinfo is OK\n", __func__);  
	return 1;
}

static void load_phoneinfo_with_imei(struct work_struct *work_s)
{
  struct file *rawdata_filp;
  char read_buf[SECTOR_SIZE];
  mm_segment_t oldfs;
  int rc;
  phoneinfo_type *pantech_phoneinfo_buff_ptr;

  static int read_count = 0;

  imei_backup_info_type *imei_backup_info_buf;

  printk(KERN_INFO "%s: read phone info start\n", __func__);

  // phoneinfo buffer init
  memset( pantech_phoneinfo_buff, 0x0, SECTOR_SIZE );  


  pantech_phoneinfo_buff[0] = 1;
  pantech_phoneinfo_buff[9] = 1;
  pantech_phoneinfo_buff_ptr = (phoneinfo_type *)&pantech_phoneinfo_buff[16];


  oldfs = get_fs();
  set_fs(KERNEL_DS);
  rawdata_filp = filp_open("/dev/block/mmcblk0p12", O_RDONLY | O_SYNC, 0);
  if( IS_ERR(rawdata_filp) )
  {
    set_fs(oldfs);
    printk(KERN_ERR "%s: filp_open error\n",__func__);
		return;
  }
  set_fs(oldfs);
  printk(KERN_INFO "%s: file open OK\n", __func__);


  rawdata_filp->f_pos = 0;
  memset( read_buf, 0x0, SECTOR_SIZE );
  // read
	if(((rawdata_filp->f_flags & O_ACCMODE) & O_RDONLY) != 0)
  {
    printk(KERN_ERR "%s: read permission denied\n",__func__);
    return;
	}

  oldfs = get_fs();
  set_fs(KERNEL_DS);
  rc = rawdata_filp->f_op->read(rawdata_filp, read_buf, SECTOR_SIZE, &rawdata_filp->f_pos);
  if (rc < 0) 
  {
    set_fs(oldfs);
    printk(KERN_ERR "%s: read phoneinfo error = %d \n",__func__,rc);
    filp_close(rawdata_filp, NULL);
		return;
  }
  set_fs(oldfs);
  memcpy(pantech_phoneinfo_buff_ptr, &read_buf[32], sizeof(phoneinfo_type));

  printk(KERN_INFO "%s: read Phoneinfo OK\n", __func__);
  rawdata_filp->f_pos = NON_SECURE_IMEI_START;
  memset( read_buf, 0x0, SECTOR_SIZE );
  printk(KERN_ERR "%s: rawdata_filp->f_pos = %x \n",__func__,NON_SECURE_IMEI_START);

  // read
	if(((rawdata_filp->f_flags & O_ACCMODE) & O_RDONLY) != 0)
  {
    printk(KERN_ERR "%s: read permission denied\n",__func__);
    return;
	}
  oldfs = get_fs();
  set_fs(KERNEL_DS);
  rc = rawdata_filp->f_op->read(rawdata_filp, read_buf, 16, &rawdata_filp->f_pos);
  if (rc < 0) {
    set_fs(oldfs);
    printk(KERN_ERR "%s: read imei error = %d \n",__func__,rc);
    filp_close(rawdata_filp, NULL);
		return;
  }
  set_fs(oldfs);

  #if 0 //test
    printk(KERN_ERR "%s : imei <%x> %x %x %x %x , %x %x %x %x, %x %x %x %x, %x %x %x \n",__func__,0,read_buf[0],read_buf[1],read_buf[2],read_buf[3],read_buf[4],read_buf[5],read_buf[6],read_buf[7],read_buf[8],read_buf[9],read_buf[10],read_buf[11],read_buf[12],read_buf[13],read_buf[14]);
  #endif

  printk(KERN_INFO "%s: read IMEI OK\n", __func__);

  imei_backup_info_buf = (imei_backup_info_type *)&read_buf[0];
  if(imei_backup_info_buf->imei_magic_num & IMEI_ADDR_MAGIC_NUM) 
  {
    memcpy(pantech_phoneinfo_buff_ptr->Imei, read_buf+4, NV_UE_IMEI_SIZE);
  }

#if (0) 	//F_PANTECH_SECBOOT
  {
    secboot_fuse_flag *secboot_flag_ptr = NULL;
    const char str_secure[] = "$3(UR3"; // secure target
    const char str_non_secure[] = "3ru(3$"; // non-secure target

    rawdata_filp->f_pos = PANTECH_SECBOOT_FLAG_START;
    memset(read_buf, 0, SECTOR_SIZE);
    printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG pos=%x\n", __func__, PANTECH_SECBOOT_FLAG_START);

    if (((rawdata_filp->f_flags & O_ACCMODE) & O_RDONLY) != 0)
    {
      printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG permission denied!\n", __func__);
      printk(KERN_ERR "%s: secure target (for safety)\n", __func__);
      strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_secure);
      return;
    }

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    rc = rawdata_filp->f_op->read(rawdata_filp, read_buf, SECTOR_SIZE, &rawdata_filp->f_pos);
    if (rc < 0) {
      set_fs(oldfs);
      printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG read failed! (%d)\n", __func__, rc);
      printk(KERN_ERR "%s: secure target (for safety)\n", __func__);
      strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_secure);
      filp_close(rawdata_filp, NULL);
      return;
    }

    set_fs(oldfs);

    printk(KERN_INFO "%s: PANTECH_SECBOOT_FLAG read done\n", __func__);

    secboot_flag_ptr = (secboot_fuse_flag *)&read_buf[0];

    if (secboot_flag_ptr->secboot_magic_num == SECBOOT_FUSE_FLAG_MAGIC_NUM)
    {
      printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG valid magic\n", __func__);

      if (secboot_flag_ptr->auth_en == SECBOOT_FUSE_NOT_BLOWN)
      {
        printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG non-secure target\n", __func__);
        strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_non_secure);
      }
      else if (secboot_flag_ptr->auth_en == SECBOOT_FUSE_BLOWN)
      {
        printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG secure target\n", __func__);
        strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_secure);
      }
      else
      {
        printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG secure target (for safety)\n", __func__);
        strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_secure);
      }
    }
    else
    {
      printk(KERN_ERR "%s: PANTECH_SECBOOT_FLAG invalid magic!\n", __func__);
      printk(KERN_ERR "%s: secure target (for safety)\n", __func__);
      strcpy(pantech_phoneinfo_buff_ptr->secure_magic_, str_secure);
    }
  }
#endif



  
  filp_close(rawdata_filp, NULL);

  if(check_phoneinfo() != 1 && read_count < 5)
  {
    schedule_delayed_work(&phoneinfo_read_wqst, HZ*10);
    read_count++;
  }

  printk(KERN_INFO "%s: read phone info end : read_count = %d\n", __func__, read_count);
  return;

}

unsigned fill_writereq(int *dloadinfo_state, struct usb_request *writereq)
{
  unsigned len = TX_BUF_SIZE;
  printk(KERN_ERR "%s: 1. port state : %d ", __func__,*dloadinfo_state);
  switch( *dloadinfo_state )
  {
    case DLOADINFO_AT_RESPONSE_STATE:
    {
		printk(KERN_ERR "%s: 2. ", __func__);
		memcpy(writereq->buf, "AT*PHONEINFO*WAIT", sizeof("AT*PHONEINFO*WAIT")-1);
		writereq->length = sizeof("AT*PHONEINFO*WAIT")-1;
		len = writereq->length;
		printk(KERN_ERR "%s: AT*PHONEINFO*WAIT", __func__);
    }
    break;
  
    case DLOADINFO_PHONE_INFO_STATE:
    {
	  printk(KERN_ERR "%s: 3. ", __func__);
      //int i;

      // header
      //    command       2byte
      //    ack_nack      2byte
      //    error_code    4byte
      //    data_length   4byte
      //    reserved      4byte

      printk(KERN_ERR "%s: case DLOADINFO_PHONE_INFO_STATE", __func__);
      memset( writereq->buf, 0x0, 16 + sizeof(phoneinfo_type) );
      len = writereq->length = fill_phoneinfo((char *)writereq->buf); 

      #if 0 //test
        for( i=3; i < len/16; i++ )
        {
          printk(KERN_ERR "%s : phoneinfo <%x> %x %x %x %x, %x %x %x %x, %x %x %x %x, %x %x %x %x\n",__func__,i*16,tx_buf[i*16],tx_buf[i*16+1],tx_buf[i*16+2],tx_buf[i*16+3],tx_buf[i*16+4],tx_buf[i*16+5],tx_buf[i*16+6],tx_buf[i*16+7],tx_buf[i*16+8],tx_buf[i*16+9],tx_buf[i*16+10],tx_buf[i*16+11],tx_buf[i*16+12],tx_buf[i*16+13],tx_buf[i*16+14],tx_buf[i*16+15]);
        }
      #endif 

      printk(KERN_ERR "%s: packet make DLOADINFO_PHONE_INFO_STATE", __func__);
    }
    break;
  
    case DLOADINFO_FINISH_STATE:
    {
	  printk(KERN_ERR "%s: 4 ", __func__);
      // header
      //    command       2byte
      //    ack_nack      2byte
      //    error_code    4byte
      //    data_length   4byte
      //    reserved      4byte

      printk(KERN_ERR "%s: case DLOADINFO_FINISH_STATE", __func__);
      memset( writereq->buf, 0x0, 16 );
      writereq->length = 16;
      len = writereq->length;

      *dloadinfo_state = DLOADINFO_NONE_STATE;
      printk(KERN_ERR "%s: set DLOADINFO_NONE_STATE", __func__);
    }
    break;
  }
  printk(KERN_ERR "%s: 5. length %d ", __func__,len);
  return len;
}
#endif
static void ghsic_data_write_tomdm(struct work_struct *w)
{
	struct gdata_port	*port;
	struct sk_buff		*skb;
	struct timestamp_info	*info;
	unsigned long		flags;
	int			ret;
#ifdef CONFIG_PANTECH_PDL_DLOAD
	const unsigned short DLOADINFO_PACKET_VERSION = 0;
#endif

	port = container_of(w, struct gdata_port, write_tomdm_w);

	if (!port || !atomic_read(&port->connected))
		return;

	spin_lock_irqsave(&port->rx_lock, flags);
	if (test_bit(TX_THROTTLED, &port->brdg.flags)) {
		spin_unlock_irqrestore(&port->rx_lock, flags);
		goto start_rx;
	}

	while ((skb = __skb_dequeue(&port->rx_skb_q))) {
		pr_debug("%s: port:%p tom:%lu pno:%d\n", __func__,
				port, port->to_modem, port->port_num);

#ifdef CONFIG_PANTECH_PDL_DLOAD

		if(check_phoneinfo() == 1)
		{
	        	if(memcmp(skb->data, "AT*PHONEINFO*RESET", (sizeof("AT*PHONEINFO*RESET") -1) ) == 0)
			{
			    	spin_unlock_irqrestore(&port->rx_lock, flags);
		            	printk(KERN_ERR "%s : PDL IDLE PLOAD REBOOT", __func__);
//	        		kernel_restart("oem-33"); //kernel_restart do not work because of device_shutdown() error
					machine_restart("oem-33");	
	            		return;
        		}
			else if(memcmp(skb->data, "AT*PHONEINFO", sizeof("AT*PHONEINFO")-1) == 0 )
        		{
	        		printk(KERN_ERR "%s: go DLOADINFO_AT_RESPONSE_STATE", __func__);
	        		dloadinfo_state = DLOADINFO_AT_RESPONSE_STATE;
      			}
			else if( dloadinfo_state == DLOADINFO_AT_RESPONSE_STATE || dloadinfo_state == DLOADINFO_PHONE_INFO_STATE )
      			{
	        		printk(KERN_ERR "%s: if %d", __func__, dloadinfo_state);
	        		if( *(unsigned int *)(skb->data) == (PHONE_INFO_CMD|(DLOADINFO_PACKET_VERSION<<16)) )
				{
	          			dloadinfo_state = DLOADINFO_PHONE_INFO_STATE;
	          			printk(KERN_ERR "%s: go DLOADINFO_PHONE_INFO_STATE", __func__);
	        		}
	        		if( *(unsigned int *)(skb->data) == (FINISH_CMD|(DLOADINFO_PACKET_VERSION<<16)) )
				{
	          			dloadinfo_state = DLOADINFO_FINISH_STATE;
	          			printk(KERN_ERR "%s: go DLOADINFO_FINISH_STATE", __func__);
	        		}
     	 		}

			if( dloadinfo_state != DLOADINFO_NONE_STATE )
			{
				int pdl_ret=0;
				struct usb_request *pdl_req=NULL;
				struct usb_ep	*ep_in=port->in;
				pdl_req=usb_ep_alloc_request(ep_in, GFP_ATOMIC);
				if(pdl_req!=NULL)
				{
					printk(KERN_ERR "%s: run cmd send_dload_packet download state %d", __func__, dloadinfo_state);
					pdl_req->buf = temp_buf;
					pdl_req->length = fill_writereq(&dloadinfo_state, pdl_req);			

					//spin_unlock_irqrestore(&port->tx_lock, flags);
					pdl_ret = usb_ep_queue(ep_in, pdl_req, GFP_KERNEL);
					//spin_lock_irqsave(&port->tx_lock, flags);
					if (pdl_ret) 
					{
						usb_ep_free_request(ep_in, pdl_req);
						printk(KERN_ERR "%s: pdl usb epIn failed\n", __func__);
					}
				}
				
			}
			

		}
#endif
		info = (struct timestamp_info *)skb->cb;
		info->rx_done_sent = get_timestamp();
		spin_unlock_irqrestore(&port->rx_lock, flags);
		ret = data_bridge_write(port->brdg.ch_id, skb);
		spin_lock_irqsave(&port->rx_lock, flags);
		if (ret < 0) {
			if (ret == -EBUSY) {
				/*flow control*/
				port->tx_throttled_cnt++;
				break;
			}
			pr_err_ratelimited("%s: write error:%d\n",
					__func__, ret);
			port->tomodem_drp_cnt++;
			dev_kfree_skb_any(skb);
			break;
		}
		port->to_modem++;
	}
	spin_unlock_irqrestore(&port->rx_lock, flags);
start_rx:
	ghsic_data_start_rx(port);
}

static void ghsic_data_epin_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gdata_port	*port = ep->driver_data;
	struct sk_buff		*skb = req->context;
	int			status = req->status;

	switch (status) {
	case 0:
		/* successful completion */
		dbg_timestamp("DL", skb);
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		/* connection gone */
		dev_kfree_skb_any(skb);
		req->buf = 0;
		usb_ep_free_request(ep, req);
		return;
	default:
		pr_err("%s: data tx ep error %d\n", __func__, status);
		break;
	}

	dev_kfree_skb_any(skb);

	spin_lock(&port->tx_lock);
	list_add_tail(&req->list, &port->tx_idle);
	spin_unlock(&port->tx_lock);

	queue_work(port->wq, &port->write_tohost_w);
}

static void
ghsic_data_epout_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gdata_port	*port = ep->driver_data;
	struct sk_buff		*skb = req->context;
	struct timestamp_info	*info = (struct timestamp_info *)skb->cb;
	int			status = req->status;
	int			queue = 0;

	switch (status) {
	case 0:
		skb_put(skb, req->actual);
		queue = 1;
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		/* cable disconnection */
		dev_kfree_skb_any(skb);
		req->buf = 0;
		usb_ep_free_request(ep, req);
		return;
	default:
		pr_err_ratelimited("%s: %s response error %d, %d/%d\n",
					__func__, ep->name, status,
				req->actual, req->length);
		dev_kfree_skb_any(skb);
		break;
	}

	spin_lock(&port->rx_lock);
	if (queue) {
		info->rx_done = get_timestamp();
		__skb_queue_tail(&port->rx_skb_q, skb);
		list_add_tail(&req->list, &port->rx_idle);
		queue_work(port->wq, &port->write_tomdm_w);
	}
	spin_unlock(&port->rx_lock);
}

static void ghsic_data_start_rx(struct gdata_port *port)
{
	struct usb_request	*req;
	struct usb_ep		*ep;
	unsigned long		flags;
	int			ret;
	struct sk_buff		*skb;
	struct timestamp_info	*info;
	unsigned int		created;

	pr_debug("%s: port:%p\n", __func__, port);
	if (!port)
		return;

	spin_lock_irqsave(&port->rx_lock, flags);
	ep = port->out;
	if (!ep) {
		spin_unlock_irqrestore(&port->rx_lock, flags);
		return;
	}

	while (atomic_read(&port->connected) && !list_empty(&port->rx_idle)) {
		if (port->rx_skb_q.qlen > ghsic_data_pend_limit_with_bridge)
			break;

		req = list_first_entry(&port->rx_idle,
					struct usb_request, list);
		list_del(&req->list);
		spin_unlock_irqrestore(&port->rx_lock, flags);

		created = get_timestamp();
		skb = alloc_skb(ghsic_data_rx_req_size, GFP_KERNEL);
		if (!skb) {
			spin_lock_irqsave(&port->rx_lock, flags);
			list_add(&req->list, &port->rx_idle);
			break;
		}
		info = (struct timestamp_info *)skb->cb;
		info->created = created;
		req->buf = skb->data;
		req->length = ghsic_data_rx_req_size;
		req->context = skb;

		info->rx_queued = get_timestamp();
		ret = usb_ep_queue(ep, req, GFP_KERNEL);
		spin_lock_irqsave(&port->rx_lock, flags);
		if (ret) {
			dev_kfree_skb_any(skb);

			pr_err_ratelimited("%s: rx queue failed\n", __func__);

			if (atomic_read(&port->connected))
				list_add(&req->list, &port->rx_idle);
			else
				usb_ep_free_request(ep, req);
			break;
		}
	}
	spin_unlock_irqrestore(&port->rx_lock, flags);
}

static void ghsic_data_start_io(struct gdata_port *port)
{
	unsigned long	flags;
	struct usb_ep	*ep_out, *ep_in;
	int		ret;

	pr_debug("%s: port:%p\n", __func__, port);

	if (!port)
		return;

	spin_lock_irqsave(&port->rx_lock, flags);
	ep_out = port->out;
	spin_unlock_irqrestore(&port->rx_lock, flags);
	if (!ep_out)
		return;

	ret = ghsic_data_alloc_requests(ep_out, &port->rx_idle,
		port->rx_q_size, ghsic_data_epout_complete, &port->rx_lock);
	if (ret) {
		pr_err("%s: rx req allocation failed\n", __func__);
		return;
	}

	spin_lock_irqsave(&port->tx_lock, flags);
	ep_in = port->in;
	spin_unlock_irqrestore(&port->tx_lock, flags);
	if (!ep_in) {
		spin_lock_irqsave(&port->rx_lock, flags);
		ghsic_data_free_requests(ep_out, &port->rx_idle);
		spin_unlock_irqrestore(&port->rx_lock, flags);
		return;
	}

	ret = ghsic_data_alloc_requests(ep_in, &port->tx_idle,
		port->tx_q_size, ghsic_data_epin_complete, &port->tx_lock);
	if (ret) {
		pr_err("%s: tx req allocation failed\n", __func__);
		spin_lock_irqsave(&port->rx_lock, flags);
		ghsic_data_free_requests(ep_out, &port->rx_idle);
		spin_unlock_irqrestore(&port->rx_lock, flags);
		return;
	}

	/* queue out requests */
	ghsic_data_start_rx(port);
}

static void ghsic_data_connect_w(struct work_struct *w)
{
	struct gdata_port	*port =
		container_of(w, struct gdata_port, connect_w);
	int			ret;

	if (!port || !atomic_read(&port->connected) ||
		!test_bit(CH_READY, &port->bridge_sts))
		return;

	pr_debug("%s: port:%p\n", __func__, port);

	ret = data_bridge_open(&port->brdg);
	if (ret) {
		pr_err("%s: unable open bridge ch:%d err:%d\n",
				__func__, port->brdg.ch_id, ret);
		return;
	}

	set_bit(CH_OPENED, &port->bridge_sts);

	ghsic_data_start_io(port);
}

static void ghsic_data_disconnect_w(struct work_struct *w)
{
	struct gdata_port	*port =
		container_of(w, struct gdata_port, disconnect_w);

	if (!test_bit(CH_OPENED, &port->bridge_sts))
		return;

	data_bridge_close(port->brdg.ch_id);
	clear_bit(CH_OPENED, &port->bridge_sts);
}

static void ghsic_data_free_buffers(struct gdata_port *port)
{
	struct sk_buff	*skb;
	unsigned long	flags;

	if (!port)
		return;

	spin_lock_irqsave(&port->tx_lock, flags);
	if (!port->in) {
		spin_unlock_irqrestore(&port->tx_lock, flags);
		return;
	}

	ghsic_data_free_requests(port->in, &port->tx_idle);

	while ((skb = __skb_dequeue(&port->tx_skb_q)))
		dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&port->tx_lock, flags);

	spin_lock_irqsave(&port->rx_lock, flags);
	if (!port->out) {
		spin_unlock_irqrestore(&port->rx_lock, flags);
		return;
	}

	ghsic_data_free_requests(port->out, &port->rx_idle);

	while ((skb = __skb_dequeue(&port->rx_skb_q)))
		dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&port->rx_lock, flags);
}

static int ghsic_data_get_port_id(const char *pdev_name)
{
	struct gdata_port *port;
	int i;

	for (i = 0; i < no_data_ports; i++) {
		port = gdata_ports[i].port;
		if (!strncmp(port->brdg.name, pdev_name, BRIDGE_NAME_MAX_LEN))
			return i;
	}

	return -EINVAL;
}

static int ghsic_data_probe(struct platform_device *pdev)
{
	struct gdata_port *port;
	int id;

	pr_debug("%s: name:%s no_data_ports= %d\n", __func__, pdev->name,
			no_data_ports);

	id = ghsic_data_get_port_id(pdev->name);
	if (id < 0 || id >= no_data_ports) {
		pr_err("%s: invalid port: %d\n", __func__, id);
		return -EINVAL;
	}

	port = gdata_ports[id].port;
	set_bit(CH_READY, &port->bridge_sts);

	/* if usb is online, try opening bridge */
	if (atomic_read(&port->connected))
		queue_work(port->wq, &port->connect_w);

	return 0;
}

/* mdm disconnect */
static int ghsic_data_remove(struct platform_device *pdev)
{
	struct gdata_port *port;
	struct usb_ep	*ep_in;
	struct usb_ep	*ep_out;
	int id;

	pr_debug("%s: name:%s\n", __func__, pdev->name);

	id = ghsic_data_get_port_id(pdev->name);
	if (id < 0 || id >= no_data_ports) {
		pr_err("%s: invalid port: %d\n", __func__, id);
		return -EINVAL;
	}

	port = gdata_ports[id].port;

	ep_in = port->in;
	if (ep_in)
		usb_ep_fifo_flush(ep_in);

	ep_out = port->out;
	if (ep_out)
		usb_ep_fifo_flush(ep_out);

	/* cancel pending writes to MDM */
	cancel_work_sync(&port->write_tomdm_w);

	ghsic_data_free_buffers(port);

	cancel_work_sync(&port->connect_w);
	if (test_and_clear_bit(CH_OPENED, &port->bridge_sts))
		data_bridge_close(port->brdg.ch_id);
	clear_bit(CH_READY, &port->bridge_sts);

	return 0;
}

static void ghsic_data_port_free(int portno)
{
	struct gdata_port	*port = gdata_ports[portno].port;
	struct platform_driver	*pdrv = &gdata_ports[portno].pdrv;

	destroy_workqueue(port->wq);
	kfree(port);

	if (pdrv)
		platform_driver_unregister(pdrv);
}

static int ghsic_data_port_alloc(unsigned port_num, enum gadget_type gtype)
{
	struct gdata_port	*port;
	struct platform_driver	*pdrv;
	char			*name;

	port = kzalloc(sizeof(struct gdata_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	name = gdata_ports[port_num].port_name;

	port->wq = create_singlethread_workqueue(name);
	if (!port->wq) {
		pr_err("%s: Unable to create workqueue:%s\n", __func__, name);
		kfree(port);
		return -ENOMEM;
	}
	port->port_num = port_num;

	/* port initialization */
	spin_lock_init(&port->rx_lock);
	spin_lock_init(&port->tx_lock);

	INIT_WORK(&port->connect_w, ghsic_data_connect_w);
	INIT_WORK(&port->disconnect_w, ghsic_data_disconnect_w);
	INIT_WORK(&port->write_tohost_w, ghsic_data_write_tohost);
	INIT_WORK(&port->write_tomdm_w, ghsic_data_write_tomdm);

	INIT_LIST_HEAD(&port->tx_idle);
	INIT_LIST_HEAD(&port->rx_idle);

	skb_queue_head_init(&port->tx_skb_q);
	skb_queue_head_init(&port->rx_skb_q);

	port->gtype = gtype;
	port->brdg.name = name;
	port->brdg.ctx = port;
	port->brdg.ops.send_pkt = ghsic_data_receive;
	port->brdg.ops.unthrottle_tx = ghsic_data_unthrottle_tx;
	gdata_ports[port_num].port = port;

	pdrv = &gdata_ports[port_num].pdrv;
	pdrv->probe = ghsic_data_probe;
	pdrv->remove = ghsic_data_remove;
	pdrv->driver.name = name;
	pdrv->driver.owner = THIS_MODULE;

	platform_driver_register(pdrv);

	pr_debug("%s: port:%p portno:%d\n", __func__, port, port_num);

	return 0;
}

void ghsic_data_disconnect(void *gptr, int port_num)
{
	struct gdata_port	*port;
	unsigned long		flags;

	pr_debug("%s: port#%d\n", __func__, port_num);

	port = gdata_ports[port_num].port;

	if (port_num > no_data_ports) {
		pr_err("%s: invalid portno#%d\n", __func__, port_num);
		return;
	}

	if (!gptr || !port) {
		pr_err("%s: port is null\n", __func__);
		return;
	}

	ghsic_data_free_buffers(port);

	/* disable endpoints */
	if (port->in) {
		usb_ep_disable(port->in);
		port->in->driver_data = NULL;
	}

	if (port->out) {
		usb_ep_disable(port->out);
		port->out->driver_data = NULL;
	}

	atomic_set(&port->connected, 0);

	spin_lock_irqsave(&port->tx_lock, flags);
	port->in = NULL;
	port->n_tx_req_queued = 0;
	clear_bit(RX_THROTTLED, &port->brdg.flags);
	spin_unlock_irqrestore(&port->tx_lock, flags);

	spin_lock_irqsave(&port->rx_lock, flags);
	port->out = NULL;
	clear_bit(TX_THROTTLED, &port->brdg.flags);
	spin_unlock_irqrestore(&port->rx_lock, flags);

	queue_work(port->wq, &port->disconnect_w);
}

int ghsic_data_connect(void *gptr, int port_num)
{
	struct gdata_port		*port;
	struct gserial			*gser;
	struct grmnet			*gr;
	unsigned long			flags;
	int				ret = 0;

	pr_debug("%s: port#%d\n", __func__, port_num);

	port = gdata_ports[port_num].port;

	if (port_num > no_data_ports) {
		pr_err("%s: invalid portno#%d\n", __func__, port_num);
		return -ENODEV;
	}

	if (!gptr || !port) {
		pr_err("%s: port is null\n", __func__);
		return -ENODEV;
	}

	if (port->gtype == USB_GADGET_SERIAL) {
		gser = gptr;

		spin_lock_irqsave(&port->tx_lock, flags);
		port->in = gser->in;
		spin_unlock_irqrestore(&port->tx_lock, flags);

		spin_lock_irqsave(&port->rx_lock, flags);
		port->out = gser->out;
		spin_unlock_irqrestore(&port->rx_lock, flags);

		port->tx_q_size = ghsic_data_serial_tx_q_size;
		port->rx_q_size = ghsic_data_serial_rx_q_size;
		gser->in->driver_data = port;
		gser->out->driver_data = port;
	} else {
		gr = gptr;

		spin_lock_irqsave(&port->tx_lock, flags);
		port->in = gr->in;
		spin_unlock_irqrestore(&port->tx_lock, flags);

		spin_lock_irqsave(&port->rx_lock, flags);
		port->out = gr->out;
		spin_unlock_irqrestore(&port->rx_lock, flags);

		port->tx_q_size = ghsic_data_rmnet_tx_q_size;
		port->rx_q_size = ghsic_data_rmnet_rx_q_size;
		gr->in->driver_data = port;
		gr->out->driver_data = port;
	}

	ret = usb_ep_enable(port->in);
	if (ret) {
		pr_err("%s: usb_ep_enable failed eptype:IN ep:%p",
				__func__, port->in);
		goto fail;
	}

	ret = usb_ep_enable(port->out);
	if (ret) {
		pr_err("%s: usb_ep_enable failed eptype:OUT ep:%p",
				__func__, port->out);
		usb_ep_disable(port->in);
		goto fail;
	}

	atomic_set(&port->connected, 1);

	spin_lock_irqsave(&port->tx_lock, flags);
	port->to_host = 0;
	port->rx_throttled_cnt = 0;
	port->rx_unthrottled_cnt = 0;
	port->unthrottled_pnd_skbs = 0;
	spin_unlock_irqrestore(&port->tx_lock, flags);

	spin_lock_irqsave(&port->rx_lock, flags);
	port->to_modem = 0;
	port->tomodem_drp_cnt = 0;
	port->tx_throttled_cnt = 0;
	port->tx_unthrottled_cnt = 0;
	spin_unlock_irqrestore(&port->rx_lock, flags);

	queue_work(port->wq, &port->connect_w);
fail:
	return ret;
}

#if defined(CONFIG_DEBUG_FS)
#define DEBUG_DATA_BUF_SIZE 4096

static unsigned int	record_timestamp;
module_param(record_timestamp, uint, S_IRUGO | S_IWUSR);

static struct timestamp_buf dbg_data = {
	.idx = 0,
	.lck = __RW_LOCK_UNLOCKED(lck)
};

/*get_timestamp - returns time of day in us */
static unsigned int get_timestamp(void)
{
	struct timeval	tval;
	unsigned int	stamp;

	if (!record_timestamp)
		return 0;

	do_gettimeofday(&tval);
	/* 2^32 = 4294967296. Limit to 4096s. */
	stamp = tval.tv_sec & 0xFFF;
	stamp = stamp * 1000000 + tval.tv_usec;
	return stamp;
}

static void dbg_inc(unsigned *idx)
{
	*idx = (*idx + 1) & (DBG_DATA_MAX-1);
}

/**
* dbg_timestamp - Stores timestamp values of a SKB life cycle
*	to debug buffer
* @event: "DL": Downlink Data
* @skb: SKB used to store timestamp values to debug buffer
*/
static void dbg_timestamp(char *event, struct sk_buff * skb)
{
	unsigned long		flags;
	struct timestamp_info	*info = (struct timestamp_info *)skb->cb;

	if (!record_timestamp)
		return;

	write_lock_irqsave(&dbg_data.lck, flags);

	scnprintf(dbg_data.buf[dbg_data.idx], DBG_DATA_MSG,
		  "%p %u[%s] %u %u %u %u %u %u\n",
		  skb, skb->len, event, info->created, info->rx_queued,
		  info->rx_done, info->rx_done_sent, info->tx_queued,
		  get_timestamp());

	dbg_inc(&dbg_data.idx);

	write_unlock_irqrestore(&dbg_data.lck, flags);
}

/* show_timestamp: displays the timestamp buffer */
static ssize_t show_timestamp(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	unsigned long	flags;
	unsigned	i;
	unsigned	j = 0;
	char		*buf;
	int		ret = 0;

	if (!record_timestamp)
		return 0;

	buf = kzalloc(sizeof(char) * DEBUG_DATA_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_lock_irqsave(&dbg_data.lck, flags);

	i = dbg_data.idx;
	for (dbg_inc(&i); i != dbg_data.idx; dbg_inc(&i)) {
		if (!strnlen(dbg_data.buf[i], DBG_DATA_MSG))
			continue;
		j += scnprintf(buf + j, DEBUG_DATA_BUF_SIZE - j,
			       "%s\n", dbg_data.buf[i]);
	}

	read_unlock_irqrestore(&dbg_data.lck, flags);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, j);

	kfree(buf);

	return ret;
}

const struct file_operations gdata_timestamp_ops = {
	.read = show_timestamp,
};

static ssize_t ghsic_data_read_stats(struct file *file,
	char __user *ubuf, size_t count, loff_t *ppos)
{
	struct gdata_port	*port;
	struct platform_driver	*pdrv;
	char			*buf;
	unsigned long		flags;
	int			ret;
	int			i;
	int			temp = 0;

	buf = kzalloc(sizeof(char) * DEBUG_DATA_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < no_data_ports; i++) {
		port = gdata_ports[i].port;
		if (!port)
			continue;
		pdrv = &gdata_ports[i].pdrv;

		spin_lock_irqsave(&port->rx_lock, flags);
		temp += scnprintf(buf + temp, DEBUG_DATA_BUF_SIZE - temp,
				"\nName:           %s\n"
				"#PORT:%d port#:   %p\n"
				"data_ch_open:	   %d\n"
				"data_ch_ready:    %d\n"
				"\n******UL INFO*****\n\n"
				"dpkts_to_modem:   %lu\n"
				"tomodem_drp_cnt:  %u\n"
				"rx_buf_len:       %u\n"
				"tx thld cnt       %u\n"
				"tx unthld cnt     %u\n"
				"TX_THROTTLED      %d\n",
				pdrv->driver.name,
				i, port,
				test_bit(CH_OPENED, &port->bridge_sts),
				test_bit(CH_READY, &port->bridge_sts),
				port->to_modem,
				port->tomodem_drp_cnt,
				port->rx_skb_q.qlen,
				port->tx_throttled_cnt,
				port->tx_unthrottled_cnt,
				test_bit(TX_THROTTLED, &port->brdg.flags));
		spin_unlock_irqrestore(&port->rx_lock, flags);

		spin_lock_irqsave(&port->tx_lock, flags);
		temp += scnprintf(buf + temp, DEBUG_DATA_BUF_SIZE - temp,
				"\n******DL INFO******\n\n"
				"dpkts_to_usbhost: %lu\n"
				"tx_buf_len:	   %u\n"
				"rx thld cnt	   %u\n"
				"rx unthld cnt	   %u\n"
				"uthld pnd skbs    %u\n"
				"RX_THROTTLED	   %d\n",
				port->to_host,
				port->tx_skb_q.qlen,
				port->rx_throttled_cnt,
				port->rx_unthrottled_cnt,
				port->unthrottled_pnd_skbs,
				test_bit(RX_THROTTLED, &port->brdg.flags));
		spin_unlock_irqrestore(&port->tx_lock, flags);

	}

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, temp);

	kfree(buf);

	return ret;
}

static ssize_t ghsic_data_reset_stats(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	struct gdata_port	*port;
	int			i;
	unsigned long		flags;

	for (i = 0; i < no_data_ports; i++) {
		port = gdata_ports[i].port;
		if (!port)
			continue;

		spin_lock_irqsave(&port->rx_lock, flags);
		port->to_modem = 0;
		port->tomodem_drp_cnt = 0;
		port->tx_throttled_cnt = 0;
		port->tx_unthrottled_cnt = 0;
		spin_unlock_irqrestore(&port->rx_lock, flags);

		spin_lock_irqsave(&port->tx_lock, flags);
		port->to_host = 0;
		port->rx_throttled_cnt = 0;
		port->rx_unthrottled_cnt = 0;
		port->unthrottled_pnd_skbs = 0;
		spin_unlock_irqrestore(&port->tx_lock, flags);
	}
	return count;
}

const struct file_operations ghsic_stats_ops = {
	.read = ghsic_data_read_stats,
	.write = ghsic_data_reset_stats,
};

static struct dentry	*gdata_dent;
static struct dentry	*gdata_dfile_stats;
static struct dentry	*gdata_dfile_tstamp;

static void ghsic_data_debugfs_init(void)
{
	gdata_dent = debugfs_create_dir("ghsic_data_xport", 0);
	if (IS_ERR(gdata_dent))
		return;

	gdata_dfile_stats = debugfs_create_file("status", 0444, gdata_dent, 0,
			&ghsic_stats_ops);
	if (!gdata_dfile_stats || IS_ERR(gdata_dfile_stats)) {
		debugfs_remove(gdata_dent);
		return;
	}

	gdata_dfile_tstamp = debugfs_create_file("timestamp", 0644, gdata_dent,
				0, &gdata_timestamp_ops);
		if (!gdata_dfile_tstamp || IS_ERR(gdata_dfile_tstamp))
			debugfs_remove(gdata_dent);
}

static void ghsic_data_debugfs_exit(void)
{
	debugfs_remove(gdata_dfile_stats);
	debugfs_remove(gdata_dfile_tstamp);
	debugfs_remove(gdata_dent);
}

#else
static void ghsic_data_debugfs_init(void) { }
static void ghsic_data_debugfs_exit(void) { }
static void dbg_timestamp(char *event, struct sk_buff * skb)
{
	return;
}
static unsigned int get_timestamp(void)
{
	return 0;
}

#endif

/*portname will be used to find the bridge channel index*/
void ghsic_data_set_port_name(const char *name, const char *xport_type)
{
	static unsigned int port_num;

	if (port_num >= NUM_PORTS) {
		pr_err("%s: setting xport name for invalid port num %d\n",
				__func__, port_num);
		return;
	}

	/*if no xport name is passed set it to xport type e.g. hsic*/
	if (!name)
		strlcpy(gdata_ports[port_num].port_name, xport_type,
				BRIDGE_NAME_MAX_LEN);
	else
		strlcpy(gdata_ports[port_num].port_name, name,
				BRIDGE_NAME_MAX_LEN);

	/*append _data to get data bridge name: e.g. serial_hsic_data*/
	strlcat(gdata_ports[port_num].port_name, "_data", BRIDGE_NAME_MAX_LEN);

	port_num++;
}

int ghsic_data_setup(unsigned num_ports, enum gadget_type gtype)
{
	int		first_port_id = no_data_ports;
	int		total_num_ports = num_ports + no_data_ports;
	int		ret = 0;
	int		i;

	if (!num_ports || total_num_ports > NUM_PORTS) {
		pr_err("%s: Invalid num of ports count:%d\n",
				__func__, num_ports);
		return -EINVAL;
	}
	pr_debug("%s: count: %d\n", __func__, num_ports);
#ifdef CONFIG_PANTECH_PDL_DLOAD
 //   INIT_DELAYED_WORK(&phoneinfo_read_wqst, load_phoneinfo_with_imei);		// P14774 RMNET + ADB mode, rebooting dump bug fix
	schedule_delayed_work(&phoneinfo_read_wqst, HZ*10);
#endif
	for (i = first_port_id; i < (num_ports + first_port_id); i++) {

		/*probe can be called while port_alloc,so update no_data_ports*/
		no_data_ports++;
		ret = ghsic_data_port_alloc(i, gtype);
		if (ret) {
			no_data_ports--;
			pr_err("%s: Unable to alloc port:%d\n", __func__, i);
			goto free_ports;
		}
	}

	/*return the starting index*/
	return first_port_id;

free_ports:
	for (i = first_port_id; i < no_data_ports; i++)
		ghsic_data_port_free(i);
		no_data_ports = first_port_id;

	return ret;
}

static int __init ghsic_data_init(void)
{
	ghsic_data_debugfs_init();

#ifdef CONFIG_PANTECH_PDL_DLOAD
    INIT_DELAYED_WORK(&phoneinfo_read_wqst, load_phoneinfo_with_imei);
#endif
	return 0;
}
module_init(ghsic_data_init);

static void __exit ghsic_data_exit(void)
{
	ghsic_data_debugfs_exit();
}
module_exit(ghsic_data_exit);
MODULE_DESCRIPTION("hsic data xport driver");
MODULE_LICENSE("GPL v2");
