#include "finger.h"
#include "config.h"
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/completion.h>
#include <linux/delay.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif
#include <mt_spi.h>

//------------------------------------------------------------------------------


/**
 * this function olny use thread ex.open
 *
 */

typedef enum DTS_STATE {
    STATE_CS_CLR,
    STATE_CS_SET,
    STATE_MI_CLR,
    STATE_MI_SET,
    STATE_MO_CLR,
    STATE_MO_SET,
    STATE_CLK_CLR,
    STATE_CLK_SET,
    STATE_RST_CLR,
    STATE_RST_SET,
    STATE_POWER_CLR,
    STATE_POWER_SET,
    STATE_EINT,
    STATE_EINT_CLR,
    STATE_EINT_SET,
    STATE_MAX,  /* array size */
} dts_status_t;
static struct pinctrl* this_pctrl; /* static pinctrl instance */
/* DTS state mapping name */
static const char* dts_state_name[STATE_MAX] = {
    "spi_cs_clr",
    "spi_cs_set",
    "spi_mi_clr",
    "spi_mi_set",
    "spi_mo_clr",
    "spi_mo_set",
    "spi_mclk_clr",
    "spi_mclk_set",
    "finger_rst_clr",
    "finger_rst_set",
	"finger_power_low",
	"finger_power_high",
    "eint",
    "eint_clr",
    "eint_set",
};
//powerking add 2016.08.19
/*static const char* dts_state_name[STATE_MAX] = {
	"spi_cs_low",
	"spi_cs_high",
	"spi_mi_low",
	"spi_mi_high",
	"spi_mo_low",
	"spi_mo_high",
	"spi_mclk_low",
	"spi_mclk_high",
	"finger_rst_low",
	"finger_rst_high",
	"finger_power_low",
	"finger_power_high",
	"eint",
	"eint_in_low",
	"eint_in_float",
};*/

/* pinctrl implementation */
inline static long dts_set_state(const char* name)
{
    long ret = 0;
    struct pinctrl_state* pin_state = 0;
    BUG_ON(!this_pctrl);
    pin_state = pinctrl_lookup_state(this_pctrl, name);

    if (IS_ERR(pin_state)) {
        pr_err("sunwave ----finger set state '%s' failed\n", name);
        ret = PTR_ERR(pin_state);
        return ret;
    }

    //printk("sunwave ---- dts_set_state = %s \n", name);
    /* select state! */
    ret = pinctrl_select_state(this_pctrl, pin_state);

    if ( ret < 0) {
        printk("sunwave --------pinctrl_select_state %s failed\n", name);
    }

    return ret;
}


inline static long dts_select_state(dts_status_t s)
{
    BUG_ON(!((unsigned int)(s) < (unsigned int)(STATE_MAX)));
    return dts_set_state(dts_state_name[s]);
}
int mt6797_dts_spi_mod(struct spi_device**  spi)
{
    dts_select_state(STATE_CS_SET);
    dts_select_state(STATE_MI_SET);
    dts_select_state(STATE_MO_SET);
    dts_select_state(STATE_CLK_SET);
    return 0;
}

int mt6797_dts_reset(struct spi_device**  spi)
{
    dts_select_state(STATE_RST_SET);
    //dts_select_state(STATE_EINT_SET);
    msleep(10);
    dts_select_state(STATE_RST_CLR);
    //dts_select_state(STATE_EINT_CLR);
    msleep(20);
    dts_select_state(STATE_RST_SET);
    //dts_select_state(STATE_EINT_SET);
    return 0;
}

//add power on 2016.08.19
int mt6737_dts_power(struct spi_device**  spi)
{
    dts_select_state(STATE_POWER_SET);
    msleep(10);

    return 0;
}


static int  mt6797_dts_gpio_init(struct spi_device**   spidev)
{
    sunwave_sensor_t*  sunwave = spi_to_sunwave(spidev);
    struct device_node* finger_irq_node = NULL;
    struct spi_device*   spi = *spidev;
    /* set power pin to high */
#ifdef CONFIG_OF
    spi->dev.of_node = of_find_compatible_node(NULL, NULL, "mediatek,sunwave-finger");
    //spi->dev.of_node = of_find_compatible_node(NULL, NULL, "mediatek,dq_finger");//powerking add 2016.08.19
    this_pctrl = devm_pinctrl_get(&spi->dev);

    if (IS_ERR(this_pctrl)) {
        //int ret = PTR_ERR(this_pctrl);
        dev_err(&spi->dev, "fwq Cannot find fp pctrl!\n");
        return -ENODEV;
    }

	mt6737_dts_power(&spi);//add power on 2016.08.19
    //dts_select_state(STATE_DEFAULT);
    mt6797_dts_reset(&spi);
    //mt6797_dts_spi_mod(&spi);
    dts_select_state(STATE_EINT);
    finger_irq_node = of_find_compatible_node(NULL, NULL, "mediatek,fingerprint-eint");
    //finger_irq_node = of_find_compatible_node(NULL, NULL, "mediatek,dq_finger");//powerking add 2016.08.19

    if (finger_irq_node) {
        u32 ints[2] = {50,100};
        of_property_read_u32_array(finger_irq_node, "debounce", ints, ARRAY_SIZE(ints));
        gpio_set_debounce(ints[0], ints[1]);
        sunwave->standby_irq = irq_of_parse_and_map(finger_irq_node, 0);
    }
    else {
        return -ENOMEM;
    }

#endif
    return 0;
}


static int  mt6797_dts_irq_hander(struct spi_device** spi)
{
    spi_to_sunwave(spi);
    return 0;
}


static int mt6797_dts_speed(struct spi_device**    spi, unsigned int speed)
{
#define SPI_MODULE_CLOCK   (100*1000*1000)
    struct mt_chip_conf* config;
    unsigned int    time = SPI_MODULE_CLOCK / speed;
    config = (struct mt_chip_conf*)(*spi)->controller_data;
    config->low_time = time / 2;
    config->high_time = time / 2;

    if (time % 2) {
        config->high_time = config->high_time + 1;
    }

    //(chip_config->low_time + chip_config->high_time);
    return 0;
}

static finger_t  finger_sensor = {
    .ver                    = 0,
#if __SUNWAVE_SPI_DMA_MODE_EN
    .attribute              = DEVICE_ATTRIBUTE_NONE,
#else // SPI FIFO MODE
    .attribute              = DEVICE_ATTRIBUTE_SPI_FIFO,
#endif

    .write_then_read        = 0,
    .irq_hander             = mt6797_dts_irq_hander,
    .irq_request            = 0,
    .irq_free               = 0,
    .init                   = mt6797_dts_gpio_init,
    .reset                  = mt6797_dts_reset,
    .speed          = mt6797_dts_speed,
};


void mt6797_dts_register_finger(sunwave_sensor_t*    sunwave)
{
    sunwave->finger = &finger_sensor;
}
EXPORT_SYMBOL_GPL(mt6797_dts_register_finger);
