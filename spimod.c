#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/io.h>
#include <linux/fs.h>		//required for fops
#include <linux/uaccess.h>	//required for 'cpoy_from_user' and 'copy_to_user'
#include <linux/signal.h>	//required for kernel-to-userspace signals

MODULE_LICENSE("GPL");

//#define IRQ_NUM     91		// interrupt line
//#define SPI_BASE   0x41E00000

#define SPI_CR   0x41E00060	// SPI control reg
#define SPI_SR   0x41E00064	// SPI status reg
#define SPI_DTR  0x41E00068	// SPI data transmit reg
#define SPI_DRR  0x41E0006C	// SPI data recieve reg
#define SPI_SSR  0x41E00070	// SPI slave select register
#define SPI_GIER 0x41E0001C	// SPI global intr enable reg
#define SPI_IER  0x41E00020	// SPI intr enable reg
#define SPI_ISR  0x41E00028	// SPI intr status reg

unsigned long *pSPI_CR;		
unsigned long *pSPI_SR;
unsigned long *pSPI_DTR;
unsigned long *pSPI_DRR;
unsigned long *pSPI_SSR;
unsigned long *pSPI_GIER;
unsigned long *pSPI_IER;
unsigned long *pSPI_ISR;				

#define DEVICE_NAME "spimod"    // device name
#define spimod_MAJOR 23         // device major number
#define BUF_LEN 128	
#define SUCCESS 0	

#define IER_DRR_READ_ENABLE (1<<4)

static int Device_Open = 0;     

static char msg[BUF_LEN];		

// write from user-space
ssize_t spimod_write(struct file *flip, const char *buf, size_t length, loff_t *offset)
{
    unsigned long lCR;
    unsigned long lSR;
    unsigned char cDRR;

    int len = (length > BUF_LEN) ? BUF_LEN : length;

    if (copy_from_user(msg, buf, len) != 0)  // read buffer from user space
       return -EFAULT;	            

    if (strcmp(msg, "e") == 0) // enable SPI command
    {
        printk("Driver enables SPI.\n");       
        iowrite32(0x0 | IER_DRR_READ_ENABLE, pSPI_IER);  
        return SUCCESS;
    } 
    else if (strcmp(msg,"d") == 0) // disable SPI IRQ command
    {
        printk("Driver disables SPI.\n");     
        iowrite32(0x0, pSPI_IER);  
                                                
        return SUCCESS;
    } 
    else if (msg[0] == 'r') // read data
    {
        iowrite32(0x0, pSPI_IER);

        iowrite32(0xFFFFFFFF, pSPI_SSR); 
        // Control - Enable (1), Master (2), CPHA 1 (4), Manual Slave select (7), Master transaction Inhibit (8)
        iowrite32(0x0 + (1<<1)+(1<<2)+(1<<4)+(1<<7)+(1<<8), pSPI_CR); 
        lCR = ioread32(pSPI_CR); 
        lSR = ioread32(pSPI_SR); 

        iowrite8(0x00 + msg[1], pSPI_DTR); // reading address

        iowrite32(0xFFFFFFFE, pSPI_SSR); 
        iowrite32(lCR & (~(1<<8)), pSPI_CR); 

        int i = 0;
        for (i=0; i++<1000 && (lSR & (1<<2)) == 0; i++) // while DTR is not empty
            lSR = ioread32(pSPI_SR);

        lSR = ioread32(pSPI_SR); 
        for (i=0; i++<10000 && (lSR & 1); i++) // while DDR is empty
            lSR = ioread32(pSPI_SR); 

        cDRR = ioread8(pSPI_DRR); 

        int j;
        for (j = 3; j >= 0; j--) // Change order of a word
        {
            iowrite8(0x0, pSPI_DTR); // write some junk to get reply
            iowrite32(lCR & (~(1<<8)), pSPI_CR); 
            lCR = ioread32(pSPI_CR); 
            lSR = ioread32(pSPI_SR); 
          
            for (i=0; i++<1000 && (lSR & (1<<2)) == 0; i++) // while DTR is not empty
                lSR = ioread32(pSPI_SR);
            //iowrite32(lCR | (1<<8), pSPI_CR); 

            lSR = ioread32(pSPI_SR); 
            for (i=0; i++<10000 && (lSR & 1); i++) // while DDR is empty
                lSR = ioread32(pSPI_SR); 

            cDRR = ioread8(pSPI_DRR); 
            msg[j] = cDRR;
        }

        lCR = ioread32(pSPI_CR); 
        lSR = ioread32(pSPI_SR); 

        iowrite32(lCR | (1<<8), pSPI_CR); 
        iowrite32(0xFFFFFFFF, pSPI_SSR); 

        iowrite32(0x0 | IER_DRR_READ_ENABLE, pSPI_IER);

        copy_to_user(buf, &msg[0], 4); 

        return SUCCESS;
    } 
    else if (msg[0] == 'w') // write data to SPI wADDD
    {
        iowrite32(0x0, pSPI_IER);

        iowrite32(0xFFFFFFFF, pSPI_SSR); 
        // Control - Enable (1), Master (2), CPHA 1 (4), Manual Slave select (7), Master transaction Inhibit (8)
        iowrite32(0x0 + (1<<1)+(1<<2)+(1<<7)+(1<<8), pSPI_CR); 
        lCR = ioread32(pSPI_CR); 
        lSR = ioread32(pSPI_SR); 

        iowrite8(0x80 + msg[1], pSPI_DTR); // writing address

        iowrite32(0xFFFFFFFE, pSPI_SSR); 
        iowrite32(lCR & (~(1<<8)), pSPI_CR); 

        int i = 0;
        for (i=0; i++<1000 && (lSR & (1<<2)) == 0; i++) // while DTR is not empty
            lSR = ioread32(pSPI_SR);

        lSR = ioread32(pSPI_SR); 
        for (i=0; i++<10000 && (lSR & 1); i++) // while DDR is empty
            lSR = ioread32(pSPI_SR); 

        cDRR = ioread8(pSPI_DRR); 

        int j;
        for (j = 5; j > 1; j--) // Change order of a word
        {
            iowrite8(msg[j], pSPI_DTR);
            iowrite32(lCR & (~(1<<8)), pSPI_CR); 
            lCR = ioread32(pSPI_CR); 
            lSR = ioread32(pSPI_SR); 
      
            for (i=0; i++<1000 && (lSR & (1<<2)) == 0; i++) // while DTR is not empty
                lSR = ioread32(pSPI_SR);

            lSR = ioread32(pSPI_SR); 
            for (i=0; i++<10000 && (lSR & 1); i++) // while DDR is empty
                lSR = ioread32(pSPI_SR); 

            cDRR = ioread8(pSPI_DRR); 
        }

        lCR = ioread32(pSPI_CR); 
        lSR = ioread32(pSPI_SR); 

        iowrite32(lCR | (1<<8), pSPI_CR); 
        iowrite32(0xFFFFFFFF, pSPI_SSR); 

        iowrite32(0x0 | IER_DRR_READ_ENABLE, pSPI_IER);

        return SUCCESS;
    } 
    else 
    {
        printk("Driver received wrong command.\n");	
        return -EFAULT;
    }
}

// read from user-space
ssize_t spimod_read(struct file *flip, char *buf, size_t length, loff_t *offset)
{
    if (copy_to_user(buf, &msg[0], strlen(msg)) != 0)   
       return -EFAULT;
    else 
    {
       //printk("Read() msg: %s \n", msg);
       return strlen(msg);
    }
}

// open /dev/spimod
static int spimod_open(struct inode *inode, struct file *file)
{
    if (Device_Open) 
    { 
        printk("spimod_open: spimod is already open\n");                
        return -EBUSY;					
    }

    Device_Open++; 
    try_module_get(THIS_MODULE);
    return 0;
}

// close /dev/spimod
static int spimod_close(struct inode *inode, struct file *file)
{
    Device_Open--;
    module_put(THIS_MODULE);
    return 0;
}

// device init and file operations
struct file_operations spimod_fops = {
    .read = spimod_read,		// read()
    .write = spimod_write,		// write()
    .open = spimod_open,		// open()
    .release = spimod_close,	// close()
};

// SPI interrupt handler
static irqreturn_t irq_handler(int spi, void* dev_id)		
{      
    unsigned long lTmp;

    iowrite32(0x0, pSPI_IER);

    lTmp = ioread32(pSPI_DRR); 
    // printk("SPI Interrupt data: %lu (dev_id hex: %x\n", lTmp);	

    iowrite32(0x0 | IER_DRR_READ_ENABLE, pSPI_IER);

    return IRQ_HANDLED;
}

static int spi_irq;

// init module      
static int __init mod_init(void)  
{
    printk(KERN_ERR "Init spimod\n");

    struct irq_data *data;
    struct device_node *np = NULL;
    int result, hw_irq;
    struct resource resource;

    unsigned long *virt_addr;
    unsigned int startAddr;

    np = of_find_node_by_name(NULL, "axi_quad_spi");
    if (!np) 
    {
        printk(KERN_ERR "axi_quad_spi: can't find compatible node in this kernel build");
        return -ENODEV;
    } 
    else 
    {
        result = of_address_to_resource(np, 0, &resource);
        if (result < 0) 
            return result;

        printk(KERN_INFO "axi_quad_spi: start at %x, reg. size=%d Bytes\n", (u32)resource.start, (u32)resource.end - (u32)resource.start);
        startAddr = (unsigned int)resource.start;

        // get a virtual irq number from device resource struct
        spi_irq = of_irq_to_resource(np, 0, &resource);
        if (spi_irq == NO_IRQ) 
        {
            printk(KERN_ERR "axi_quad_spi: of_irq_to_resource failed...\n");
            of_node_put(np);
            return -ENODEV;
        }
        printk(KERN_INFO "axi_quad_spi: virq=%d\n", spi_irq);
        // check the hw irq is correct
        data = irq_get_irq_data(spi_irq);
        hw_irq = WARN_ON(!data)?0:data->hwirq;
        printk(KERN_INFO "axi_quad_spi: hw_irq=%d\n", hw_irq);
    }

    if (request_irq(spi_irq, irq_handler, IRQF_DISABLED, DEVICE_NAME, NULL))  //request spi interrupt
    {
        printk(KERN_ERR "Not Registered IRQ\n");
        return -EBUSY;
    }
    printk(KERN_ERR "SPI IRQ Registered\n");

    pSPI_CR   = ioremap_nocache(SPI_CR, 0x4);	
    pSPI_SR   = ioremap_nocache(SPI_SR, 0x4);	
    pSPI_DTR  = ioremap_nocache(SPI_DTR, 0x4);	
    pSPI_DRR  = ioremap_nocache(SPI_DRR, 0x4);	
    pSPI_SSR  = ioremap_nocache(SPI_SSR, 0x4);	
    pSPI_GIER = ioremap_nocache(SPI_GIER, 0x4);
    pSPI_IER  = ioremap_nocache(SPI_IER, 0x4);
    pSPI_ISR  = ioremap_nocache(SPI_ISR, 0x4);								

    iowrite32(0xFFFFFFFF, pSPI_SSR); 
    // Control - Enable (1), Master (2), CPHA 1 (4), Manual Slave select (7), Master transaction Inhibit (8)
    iowrite32(0x0 + (1<<1)+(1<<2)+(1<<4)+(1<<7)+(1<<8), pSPI_CR); 

    // Enable interrupts
    iowrite32(0x80000000, pSPI_GIER); 
    iowrite32(0x00000003, pSPI_IER); 

    // Manual node creation
    if (register_chrdev(spimod_MAJOR, DEVICE_NAME, &spimod_fops))
       printk("Error: cannot register to major device %d\n", spimod_MAJOR);
	
    printk("Type: mknod /dev/%s c %d 0\n", DEVICE_NAME, spimod_MAJOR);
    printk("And remove it after unloading the module\n");

    return SUCCESS;
} 

// exit module
static void __exit mod_exit(void)  		
{
    iounmap(pSPI_CR);	
    iounmap(pSPI_SR);	
    iounmap(pSPI_DTR);	
    iounmap(pSPI_DRR);	
    iounmap(pSPI_SSR);	
    iounmap(pSPI_GIER);	
    iounmap(pSPI_IER);
    iounmap(pSPI_ISR);

    free_irq(spi_irq, NULL); 
    unregister_chrdev(spimod_MAJOR, "spimod");	
    printk(KERN_ERR "Exit spimod Module\n");	
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR ("kyuriy");
MODULE_DESCRIPTION("MMI dirver for the Xilinx AXI SPI");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("custom:spimod");
