/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2011, Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_mot.h"
#include "mdp4.h"

/* MIPI_MOT_PANEL_ESD_TEST is used to run the ESD recovery stress test */
/* #define MIPI_MOT_PANEL_ESD_TEST	1 */
#define MIPI_MOT_PANEL_ESD_CNT_MAX	3


static struct mipi_mot_panel *mot_panel;

static char manufacture_id[2] = {DCS_CMD_READ_DA, 0x00}; /* DTYPE_DCS_READ */
static char controller_ver[2] = {DCS_CMD_READ_DB, 0x00};
static char controller_drv_ver[2] = {DCS_CMD_READ_DC, 0x00};
static char display_on[2] = {DCS_CMD_SET_DISPLAY_ON, 0x00};
static char get_power_mode[2] = {DCS_CMD_GET_POWER_MODE, 0x00};

static struct dsi_cmd_desc mot_manufacture_id_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 0, sizeof(manufacture_id), manufacture_id};

static struct dsi_cmd_desc mot_controller_ver_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(controller_ver), controller_ver};

static struct dsi_cmd_desc mot_controller_drv_ver_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(controller_drv_ver),
							controller_drv_ver};

static struct dsi_cmd_desc mot_display_on_cmd = {
	DTYPE_DCS_WRITE, 1, 0, 0, 5, sizeof(display_on), display_on};

static struct dsi_cmd_desc mot_get_pwr_mode_cmd = {
	DTYPE_DCS_READ,  1, 0, 1, 0, sizeof(get_power_mode), get_power_mode};


int mipi_mot_panel_on(struct msm_fb_data_type *mfd)
{
	struct dsi_buf *tp = mot_panel->mot_tx_buf;

	mdp4_dsi_cmd_dma_busy_wait(mfd);
	mipi_dsi_mdp_busy_wait(mfd);

	mipi_dsi_buf_init(tp);
	mipi_dsi_cmds_tx(mfd, tp, &mot_display_on_cmd, 1);

	return 0;
}

static int32 get_panel_info(struct msm_fb_data_type *mfd,
				struct  mipi_mot_panel *mot_panel,
				struct dsi_cmd_desc *cmd)
{
	struct dsi_buf *rp, *tp;
	uint32 *lp;
	int ret;

	tp = mot_panel->mot_tx_buf;
	rp = mot_panel->mot_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	ret = mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 1);
	if (!ret)
		ret = -1;
	else {
		lp = (uint32 *)rp->data;
		ret = (int)*lp;
	}

	return ret;
}

void mipi_mot_set_mot_panel(struct mipi_mot_panel *mot_panel_ptr)
{
	mot_panel = mot_panel_ptr;
}

u8 mipi_mode_get_pwr_mode(struct msm_fb_data_type *mfd)
{
	struct dsi_cmd_desc *cmd;
	u8 power_mode;

	cmd = &mot_get_pwr_mode_cmd;
	power_mode = get_panel_info(mfd, mot_panel, cmd);

	pr_debug("%s: panel power mode = 0x%x\n", __func__, power_mode);
	return power_mode;
}

u16 mipi_mot_get_manufacture_id(struct msm_fb_data_type *mfd)
{
	struct dsi_cmd_desc *cmd;
	static int manufacture_id = INVALID_VALUE;

	if (manufacture_id == INVALID_VALUE) {
		if (mot_panel == NULL) {
			pr_err("%s: invalid mot_panel\n", __func__);
			return -1;
		}

		cmd = &mot_manufacture_id_cmd;
		manufacture_id = get_panel_info(mfd, mot_panel, cmd);
	}

	return manufacture_id;
}


u16 mipi_mot_get_controller_ver(struct msm_fb_data_type *mfd)
{
	struct dsi_cmd_desc *cmd;
	static int controller_ver = INVALID_VALUE;

	if (controller_ver == INVALID_VALUE) {
		if (mot_panel == NULL) {
			pr_err("%s: invalid mot_panel\n", __func__);
			return -1;
		}

		cmd = &mot_controller_ver_cmd;
		controller_ver =  get_panel_info(mfd, mot_panel, cmd);
	}

	return controller_ver;
}


u16 mipi_mot_get_controller_drv_ver(struct msm_fb_data_type *mfd)
{
	struct dsi_cmd_desc *cmd;
	static int controller_drv_ver = INVALID_VALUE;

	if (controller_drv_ver == INVALID_VALUE) {
		if (mot_panel == NULL) {
			pr_err("%s: invalid mot_panel\n", __func__);
			return -1;
		}

		cmd = &mot_controller_drv_ver_cmd;
		controller_drv_ver = get_panel_info(mfd, mot_panel, cmd);
	}

	return controller_drv_ver;
}

static int esd_recovery_start(struct msm_fb_data_type *mfd)
{
	struct msm_fb_panel_data *pdata = NULL;

	pr_info("MIPI MOT: ESD recovering is started\n");

	pdata = (struct msm_fb_panel_data *)mfd->pdev->dev.platform_data;

	mfd->panel_power_on = FALSE;
	pdata->off(mfd->pdev);
	msleep(20);
	pdata->on(mfd->pdev);
	mfd->panel_power_on = TRUE;
	mfd->dma_fnc(mfd);

	pr_info("MIPI MOT: ESD recovering is done\n");

	return 0;
}

static int mipi_mot_esd_detection(struct msm_fb_data_type *mfd)
{

	u8 expected_mode, pwr_mode = 0;
	static u16 manufacture_id = 0xff;
	u16 rd_manufacture_id;
	int ret = 0;

	if (atomic_read(&mot_panel->state) == MOT_PANEL_OFF)
		return 0;

	if (manufacture_id == 0xff)
		manufacture_id = get_panel_info(mfd, mot_panel,
						&mot_manufacture_id_cmd);

	if (atomic_read(&mot_panel->state) == MOT_PANEL_ON)
		expected_mode = 0x9c;
	else
		expected_mode = 0x98;

	mutex_lock(&mfd->dma->ov_mutex);

	mipi_set_tx_power_mode(0);
	mdp4_dsi_cmd_dma_busy_wait(mfd);
	mipi_dsi_mdp_busy_wait(mfd);
	pwr_mode = mipi_mode_get_pwr_mode(mot_panel->mfd);
	mutex_unlock(&mfd->dma->ov_mutex);

	/*
	 * There is a issue of the mipi_dsi_cmds_rx(), and case# 00743147
	 * is opened for this API, but the patch from QCOm doesn't fix the
	 * problem. In this commit will include the change from QCOM also.
	 *
	 * During the ESD test, if there is any issue of the MDP/DSI or
	 * panel, this mipi_dsi_cmds_rx() will return the data of the previous
	 * read.
	 * To workaround this problem, we will provide 2 reads, and if ESD
	 * happens, then 1 of data will be wrong, then the ESD will be kicked
	 * in.
	 * For Video mode, the blanking time will not big enough for 2 read
	 * commands, therefore we will free the DSI bus for 100msec after
	 * the first read.
	 */
	msleep(100);

	mipi_set_tx_power_mode(0);
	mdp4_dsi_cmd_dma_busy_wait(mfd);
	mipi_dsi_mdp_busy_wait(mfd);
	rd_manufacture_id = get_panel_info(mfd, mot_panel,
						&mot_manufacture_id_cmd);
	mutex_unlock(&mfd->dma->ov_mutex);

	if ((pwr_mode != expected_mode) ||
		(rd_manufacture_id != manufacture_id)) {
		pr_err("%s: Power mode in incorrect state or wrong. "
			"manufacture_id. Cur_mode=0x%x Expected_mode=0x%x "
			" stored manufacture_id=0x%x Read=0x%x\n",
			__func__, pwr_mode, expected_mode,
			manufacture_id, rd_manufacture_id);
		ret = -1;
	}

	return ret;
}

void mipi_mot_esd_work(void)
{
	struct msm_fb_data_type *mfd;
	int ret, i;
#ifdef MIPI_MOT_PANEL_ESD_TEST
	static int esd_count;
	static int esd_trigger_cnt;
#endif

	mfd = mot_panel->mfd;

	if (mot_panel == NULL) {
		pr_err("%s: invalid mot_panel\n", __func__);
		return;
	}

	/*
	 * Try to run ESD detection on the panel MOT_PANEL_ESD_NUM_TRY_MAX
	 * times, if the response data are incorrect then start to reset the
	 * MDP and panel
	 */

	for (i = 0; i < MOT_PANEL_ESD_NUM_TRY_MAX; i++) {
		ret =  mipi_mot_esd_detection(mfd);
		if (!ret)
			break;

		msleep(100);
	}

	if (i >= MOT_PANEL_ESD_NUM_TRY_MAX)
		esd_recovery_start(mot_panel->mfd);

#ifdef MIPI_MOT_PANEL_ESD_TEST
	if (esd_count++ >= MIPI_MOT_PANEL_ESD_CNT_MAX) {
		pr_info("%s(%d): start to ESD test\n", __func__,
							esd_trigger_cnt++);
		esd_count = 0;
		esd_recovery_start(mot_panel->mfd);
	} else
		pr_info("%s(%d):is called.\n", __func__, esd_trigger_cnt++);
#endif
	queue_delayed_work(mot_panel->esd_wq, &mot_panel->esd_work,
						MOT_PANEL_ESD_CHECK_PERIOD);
}