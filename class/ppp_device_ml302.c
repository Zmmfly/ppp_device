/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author        Notes
 * 2020-03-18     Zmmfly        the first version
 */

#include <ppp_device.h>
#include <ppp_chat.h>
#include <rtdevice.h>
#include <board.h>
#include <at.h>

#define DBG_TAG    "ppp.ml302"

#ifdef PPP_DEVICE_DEBUG
#define DBG_LVL   DBG_LOG
#else
#define DBG_LVL   DBG_INFO
#endif
#include <rtdbg.h>

#define ML302_PIN_PWRKEY    GET_PIN(A, 10)
#define ML302_PIN_PWRCTL    GET_PIN(A, 15)
#define ML302_PIN_NRESET    GET_PIN(A, 9)
#define ML302_PIN_NETSTA    GET_PIN(A, 8)
#define ML302_PIN_MODSTA    GET_PIN(B, 13)

#define ML302_WARTING_TIME_BASE 500

#define ATRESP(name,bufsz,line,timeout) at_response_t name = at_create_resp(bufsz, line, timeout)
#define DELR(name) at_delete_resp(name)
#define EXEC_CMD(c,r,...) at_obj_exec_cmd(c, r, __VA_ARGS__)

#ifndef PKG_USING_CMUX
static const struct modem_chat_data rst_mcd[] =
{
    {"+++",          MODEM_CHAT_RESP_NOT_NEED,        10, 1, RT_FALSE},
    {"ATH",       MODEM_CHAT_RESP_NOT_NEED,  1,  1, RT_FALSE},
};

static const struct modem_chat_data mcd[] =
{
    {"AT",           MODEM_CHAT_RESP_OK,        10, 1, RT_FALSE},
    {"ATE0",         MODEM_CHAT_RESP_OK,        1,  1, RT_FALSE},
    {"AT+CREG=0",    MODEM_CHAT_RESP_OK,        1,  1, RT_FALSE},
    {PPP_APN_CMD,    MODEM_CHAT_RESP_OK,        1,  5, RT_FALSE},
    //{"AT+CGACT=1,1", MODEM_CHAT_RESP_OK,        1, 30, RT_FALSE},
    {PPP_DAIL_CMD,   MODEM_CHAT_RESP_CONNECT,   10, 30, RT_FALSE},
};
#else
static const struct modem_chat_data mcd[] =
{
    {"AT",           MODEM_CHAT_RESP_NOT_NEED,  10, 1, RT_FALSE},
    // {"AT+CREG=0",    MODEM_CHAT_RESP_OK,        1,  1, RT_FALSE},
    {PPP_APN_CMD,    MODEM_CHAT_RESP_NOT_NEED,  1,  5, RT_FALSE},
    //{"AT+CGACT=1,1", MODEM_CHAT_RESP_OK,        1, 30, RT_FALSE},
    {PPP_DAIL_CMD,   MODEM_CHAT_RESP_NOT_NEED,  10, 10, RT_FALSE},
};
#endif

#if 1
#define ATRESP(name,bufsz,line,timeout) at_response_t name = at_create_resp(bufsz, line, timeout)
#define DELR(name) at_delete_resp(name)

void ml302_powerup()
{
    rt_pin_write(ML302_PIN_PWRCTL, PIN_LOW);
    rt_thread_mdelay(200);
    rt_pin_write(ML302_PIN_PWRKEY, PIN_HIGH);
    rt_thread_mdelay(ML302_WARTING_TIME_BASE*4+200);
    rt_pin_write(ML302_PIN_PWRKEY, PIN_LOW);
    rt_thread_mdelay(10000);
    LOG_W("ML302 Power up");
}

void ml302_powerdown()
{
    rt_pin_write(ML302_PIN_PWRKEY, PIN_LOW);
    rt_pin_write(ML302_PIN_PWRCTL, PIN_HIGH);
    rt_thread_mdelay(200);
    LOG_W("ML302 Power down");
}

/**
 * @brief Chat for call *99#
 * 
 * @param dev 
 * @return int 
 */
int ml302_chat(struct ppp_device *dev)
{
    rt_err_t ret;
    ATRESP(resp, 64, 3, 500);

    LOG_W("Start ml302 chat");

    //关回显
    // ret = EXEC_CMD(dev->at, resp, "ATE0");
    // if (ret != RT_EOK)
    // {
    //     LOG_E("ATE0 failed");
    //     goto exit;
    // }

    //关状态URC
    ret = EXEC_CMD(dev->at, resp, "AT+CREG=0");
    if (ret != RT_EOK)
    {
        LOG_E("Disable state urc fail");
        goto exit;
    }
    rt_thread_mdelay(200);

    //设置APN
    ret = EXEC_CMD(dev->at, resp, PPP_APN_CMD);
    if (ret != RT_EOK)
    {
        LOG_E("Set APN failed!");
        goto exit;
    }
    rt_thread_mdelay(200);

    //拨号
    at_obj_set_resp_ok(dev->at, "CONNECT");
    ret = EXEC_CMD(dev->at, resp, PPP_DAIL_CMD);
    if (ret != RT_EOK)
    {
        LOG_E("Call failed!");
        goto exit;
    }
    at_obj_set_resp_ok(dev->at, NULL);
    rt_thread_mdelay(200);

exit:
    DELR(resp);
    return ret;
}

#include "mod_ml302.h"

int ml302_ppp_init(struct ppp_device *dev)
{
    ATRESP(resp, 64, 3, 200);
    rt_err_t ret;

    LOG_W("ML302 ppp init, enable at client");
    ppp_device_at_enable(dev);
    rt_thread_mdelay(10);

    // Wait for response
    ret = at_client_obj_wait_connect(dev->at, 1000);
    if (ret != RT_EOK)
    {
        LOG_E("Module no response");
        goto exit;
    }

    rt_thread_mdelay(200);

    EXEC_CMD(dev->at, resp, "ATE0");

    rt_thread_mdelay(200);

    // Get Info
    ret = ml302_get_info_by_client(dev->at, &ml302_info);
    if (ret != RT_EOK)
    {
        LOG_E("Get info failed!");
        goto exit;
    }
    LOG_W("Get info success!");

    rt_thread_mdelay(200);

    // Chat
    ret = ml302_chat(dev);
    if (ret != RT_EOK)
    {
        LOG_E("Chat failed!");
        goto exit;
    }
    LOG_W("chat success!");

exit:
    ppp_device_at_disable(dev);
    rt_thread_mdelay(10);
    DELR(resp);
    return ret;
}

static rt_err_t ppp_ml302_prepare(struct ppp_device *device)
{
    ml302_powerdown();
    ml302_powerup();
    return ml302_ppp_init(device);
}
#else
static rt_err_t ppp_ml302_prepare(struct ppp_device *device)
{
#ifndef PKG_USING_CMUX
    if (device->power_pin >= 0)
    {
        //Cut down power and repower
        rt_pin_write(ML302_PIN_PWRCTL, PIN_HIGH);
        rt_thread_mdelay(ML302_WARTING_TIME_BASE);
        rt_pin_write(ML302_PIN_PWRCTL, PIN_LOW);

        rt_thread_mdelay(ML302_WARTING_TIME_BASE*2);
        //Boot
        rt_pin_write(ML302_PIN_PWRKEY, PIN_HIGH);
        rt_thread_mdelay(ML302_WARTING_TIME_BASE*4+200);
        rt_pin_write(ML302_PIN_PWRKEY, PIN_LOW);
        rt_thread_mdelay(10000);
    }
    else
    {
        rt_err_t err;
        err = modem_chat(device->uart, rst_mcd, sizeof(rst_mcd) / sizeof(rst_mcd[0]));
        if (err)
            return err;
    }
#endif
    return modem_chat(device->uart, mcd, sizeof(mcd) / sizeof(mcd[0]));
}
#endif

/* ppp_ml302_ops for ppp_device_ops , a common interface */
static struct ppp_device_ops ml302_ops =
{
    .prepare = ppp_ml302_prepare,
};

/**
 * register ml302 into ppp_device
 *
 * @return  =0:   ppp_device register successfully
 *          <0:   ppp_device register failed
 */
int ppp_ml302_register(void)
{
    struct ppp_device *ppp_device = RT_NULL;

    ppp_device = rt_malloc(sizeof(struct ppp_device));
    if(ppp_device == RT_NULL)
    {
        LOG_E("No memory for struct ml302 ppp_device.");
        return -RT_ENOMEM;
    }

    ppp_device->power_pin = ML302_PIN_PWRCTL;
    if (ppp_device->power_pin >= 0)
    {
        // rt_pin_mode(ppp_device->power_pin, PIN_MODE_OUTPUT);
        rt_pin_write(ppp_device->power_pin, PIN_LOW);
    }

    ppp_device->ops = &ml302_ops;
    LOG_D("ppp ml302 is registering ppp_device");
    return ppp_device_register(ppp_device, PPP_DEVICE_NAME, RT_NULL, RT_NULL);
}
INIT_COMPONENT_EXPORT(ppp_ml302_register);
