/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "hwdriver"

/**
 * @file
 *
 * Hardware driver handling in libsigrok.
 */

/**
 * @defgroup grp_driver Hardware drivers
 *
 * Hardware driver handling in libsigrok.
 *
 * @{
 */

static struct sr_config_info sr_config_info_data[] = {
	{SR_CONF_CONN, SR_T_CHAR, "conn",
		"Connection", NULL},
	{SR_CONF_SERIALCOMM, SR_T_CHAR, "serialcomm",
		"Serial communication", NULL},
	{SR_CONF_SAMPLERATE, SR_T_UINT64, "samplerate",
		"Sample rate", NULL},
	{SR_CONF_CAPTURE_RATIO, SR_T_UINT64, "captureratio",
		"Pre-trigger capture ratio", NULL},
	{SR_CONF_PATTERN_MODE, SR_T_CHAR, "pattern",
		"Pattern generator mode", NULL},
	{SR_CONF_TRIGGER_TYPE, SR_T_CHAR, "triggertype",
		"Trigger types", NULL},
	{SR_CONF_EXTERNAL_CLOCK, SR_T_BOOL, "external_clock",
		"External clock mode", NULL},
	{SR_CONF_SWAP, SR_T_BOOL, "swap",
		"Swap channel order", NULL},
	{SR_CONF_RLE, SR_T_BOOL, "rle",
		"Run Length Encoding", NULL},
	{SR_CONF_TRIGGER_SLOPE, SR_T_UINT64, "triggerslope",
		"Trigger slope", NULL},
	{SR_CONF_TRIGGER_SOURCE, SR_T_CHAR, "triggersource",
		"Trigger source", NULL},
	{SR_CONF_HORIZ_TRIGGERPOS, SR_T_FLOAT, "horiz_triggerpos",
		"Horizontal trigger position", NULL},
	{SR_CONF_BUFFERSIZE, SR_T_UINT64, "buffersize",
		"Buffer size", NULL},
	{SR_CONF_TIMEBASE, SR_T_RATIONAL_PERIOD, "timebase",
		"Time base", NULL},
	{SR_CONF_FILTER, SR_T_CHAR, "filter",
		"Filter targets", NULL},
	{SR_CONF_VDIV, SR_T_RATIONAL_VOLT, "vdiv",
		"Volts/div", NULL},
	{SR_CONF_COUPLING, SR_T_CHAR, "coupling",
		"Coupling", NULL},
	{SR_CONF_DATALOG, SR_T_BOOL, "datalog",
		"Datalog", NULL},
	{SR_CONF_SPL_WEIGHT_FREQ, SR_T_CHAR, "spl_weight_freq",
		"Sound pressure level frequency weighting", NULL},
	{SR_CONF_SPL_WEIGHT_TIME, SR_T_CHAR, "spl_weight_time",
		"Sound pressure level time weighting", NULL},
	{SR_CONF_HOLD_MAX, SR_T_BOOL, "hold_max",
		"Hold max", NULL},
	{SR_CONF_HOLD_MIN, SR_T_BOOL, "hold_min",
		"Hold min", NULL},
	{SR_CONF_SPL_MEASUREMENT_RANGE, SR_T_UINT64_RANGE, "spl_meas_range",
		"Sound pressure level measurement range", NULL},
	{SR_CONF_VOLTAGE_THRESHOLD, SR_T_DOUBLE_RANGE, "voltage_threshold",
		"Voltage threshold", NULL },
	{SR_CONF_POWER_OFF, SR_T_BOOL, "power_off",
		"Power off", NULL},
	{SR_CONF_DATA_SOURCE, SR_T_CHAR, "data_source",
		"Data source", NULL},
	{SR_CONF_NUM_LOGIC_PROBES, SR_T_INT32, "logic_probes",
		"Number of logic probes", NULL},
	{SR_CONF_NUM_ANALOG_PROBES, SR_T_INT32, "analog_probes",
		"Number of analog probes", NULL},
	{0, 0, NULL, NULL, NULL},
};

/** @cond PRIVATE */
#ifdef HAVE_HW_ANALOG_DISCOVERY
extern SR_PRIV struct sr_dev_driver analog_discovery_driver_info;
#endif
#ifdef HAVE_HW_APPA_55II
extern SR_PRIV struct sr_dev_driver appa_55ii_driver_info;
#endif
#ifdef HAVE_HW_BRYMEN_DMM
extern SR_PRIV struct sr_dev_driver brymen_bm857_driver_info;
#endif
#ifdef HAVE_HW_CEM_DT_885X
extern SR_PRIV struct sr_dev_driver cem_dt_885x_driver_info;
#endif
#ifdef HAVE_HW_CENTER_3XX
extern SR_PRIV struct sr_dev_driver center_309_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_k204_driver_info;
#endif
#ifdef HAVE_HW_COLEAD_SLM
extern SR_PRIV struct sr_dev_driver colead_slm_driver_info;
#endif
#ifdef HAVE_HW_DEMO
extern SR_PRIV struct sr_dev_driver demo_driver_info;
#endif
#ifdef HAVE_HW_GMC_MH_1X_2X
extern SR_PRIV struct sr_dev_driver gmc_mh_1x_2x_rs232_driver_info;
#endif
#ifdef HAVE_HW_HAMEG_HMO
extern SR_PRIV struct sr_dev_driver hameg_hmo_driver_info;
#endif
#ifdef HAVE_HW_IKALOGIC_SCANALOGIC2
extern SR_PRIV struct sr_dev_driver ikalogic_scanalogic2_driver_info;
#endif
#ifdef HAVE_HW_IKALOGIC_SCANAPLUS
extern SR_PRIV struct sr_dev_driver ikalogic_scanaplus_driver_info;
#endif
#ifdef HAVE_HW_KECHENG_KC_330B
extern SR_PRIV struct sr_dev_driver kecheng_kc_330b_driver_info;
#endif
#ifdef HAVE_HW_LASCAR_EL_USB
extern SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info;
#endif
#ifdef HAVE_HW_MIC_985XX
extern SR_PRIV struct sr_dev_driver mic_98581_driver_info;
extern SR_PRIV struct sr_dev_driver mic_98583_driver_info;
#endif
#ifdef HAVE_HW_NORMA_DMM
extern SR_PRIV struct sr_dev_driver norma_dmm_driver_info;
#endif
#ifdef HAVE_HW_OLS
extern SR_PRIV struct sr_dev_driver ols_driver_info;
#endif
#ifdef HAVE_HW_RIGOL_DS
extern SR_PRIV struct sr_dev_driver rigol_ds_driver_info;
#endif
#ifdef HAVE_HW_SALEAE_LOGIC16
extern SR_PRIV struct sr_dev_driver saleae_logic16_driver_info;
#endif
#ifdef HAVE_HW_TELEINFO
extern SR_PRIV struct sr_dev_driver teleinfo_driver_info;
#endif
#ifdef HAVE_HW_TONDAJ_SL_814
extern SR_PRIV struct sr_dev_driver tondaj_sl_814_driver_info;
#endif
#ifdef HAVE_HW_UNI_T_UT32X
extern SR_PRIV struct sr_dev_driver uni_t_ut32x_driver_info;
#endif
#ifdef HAVE_HW_VICTOR_DMM
extern SR_PRIV struct sr_dev_driver victor_dmm_driver_info;
#endif
#ifdef HAVE_HW_ZEROPLUS_LOGIC_CUBE
extern SR_PRIV struct sr_dev_driver zeroplus_logic_cube_driver_info;
#endif
#ifdef HAVE_HW_ASIX_SIGMA
extern SR_PRIV struct sr_dev_driver asix_sigma_driver_info;
#endif
#ifdef HAVE_HW_CHRONOVU_LA8
extern SR_PRIV struct sr_dev_driver chronovu_la8_driver_info;
#endif
#ifdef HAVE_HW_LINK_MSO19
extern SR_PRIV struct sr_dev_driver link_mso19_driver_info;
#endif
#ifdef HAVE_HW_ALSA
extern SR_PRIV struct sr_dev_driver alsa_driver_info;
#endif
#ifdef HAVE_HW_FX2LAFW
extern SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
#endif
#ifdef HAVE_HW_HANTEK_DSO
extern SR_PRIV struct sr_dev_driver hantek_dso_driver_info;
#endif
#ifdef HAVE_HW_AGILENT_DMM
extern SR_PRIV struct sr_dev_driver agdmm_driver_info;
#endif
#ifdef HAVE_HW_FLUKE_DMM
extern SR_PRIV struct sr_dev_driver flukedmm_driver_info;
#endif
#ifdef HAVE_HW_SERIAL_DMM
extern SR_PRIV struct sr_dev_driver bbcgm_m2110_driver_info;
extern SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info;
extern SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info;
extern SR_PRIV struct sr_dev_driver metex_me31_driver_info;
extern SR_PRIV struct sr_dev_driver peaktech_3410_driver_info;
extern SR_PRIV struct sr_dev_driver mastech_mas345_driver_info;
extern SR_PRIV struct sr_dev_driver va_va18b_driver_info;
extern SR_PRIV struct sr_dev_driver va_va40b_driver_info;
extern SR_PRIV struct sr_dev_driver metex_m3640d_driver_info;
extern SR_PRIV struct sr_dev_driver metex_m4650cr_driver_info;
extern SR_PRIV struct sr_dev_driver peaktech_4370_driver_info;
extern SR_PRIV struct sr_dev_driver pce_pce_dm32_driver_info;
extern SR_PRIV struct sr_dev_driver radioshack_22_168_driver_info;
extern SR_PRIV struct sr_dev_driver radioshack_22_805_driver_info;
extern SR_PRIV struct sr_dev_driver radioshack_22_812_driver_info;
extern SR_PRIV struct sr_dev_driver tecpel_dmm_8061_ser_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_m3650d_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_m4650cr_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc820_ser_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc830_ser_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc840_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut60a_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut60e_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61b_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61c_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61d_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61e_ser_driver_info;
extern SR_PRIV struct sr_dev_driver iso_tech_idm103n_driver_info;
#endif
#ifdef HAVE_HW_UNI_T_DMM
extern SR_PRIV struct sr_dev_driver tecpel_dmm_8061_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut60a_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut60e_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61b_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61c_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61d_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61e_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc820_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc830_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc840_driver_info;
extern SR_PRIV struct sr_dev_driver tenma_72_7745_driver_info;
#endif
/** @endcond */

static struct sr_dev_driver *drivers_list[] = {
#ifdef HAVE_HW_ANALOG_DISCOVERY
	&analog_discovery_driver_info,
#endif
#ifdef HAVE_HW_APPA_55II
	&appa_55ii_driver_info,
#endif
#ifdef HAVE_HW_BRYMEN_DMM
	&brymen_bm857_driver_info,
#endif
#ifdef HAVE_HW_CEM_DT_885X
	&cem_dt_885x_driver_info,
#endif
#ifdef HAVE_HW_CENTER_3XX
	&center_309_driver_info,
	&voltcraft_k204_driver_info,
#endif
#ifdef HAVE_HW_COLEAD_SLM
	&colead_slm_driver_info,
#endif
#ifdef HAVE_HW_DEMO
	&demo_driver_info,
#endif
#ifdef HAVE_HW_GMC_MH_1X_2X
	&gmc_mh_1x_2x_rs232_driver_info,
#endif
#ifdef HAVE_HW_HAMEG_HMO
	&hameg_hmo_driver_info,
#endif
#ifdef HAVE_HW_IKALOGIC_SCANALOGIC2
	&ikalogic_scanalogic2_driver_info,
#endif
#ifdef HAVE_HW_IKALOGIC_SCANAPLUS
	&ikalogic_scanaplus_driver_info,
#endif
#ifdef HAVE_HW_KECHENG_KC_330B
	&kecheng_kc_330b_driver_info,
#endif
#ifdef HAVE_HW_LASCAR_EL_USB
	&lascar_el_usb_driver_info,
#endif
#ifdef HAVE_HW_MIC_985XX
	&mic_98581_driver_info,
	&mic_98583_driver_info,
#endif
#ifdef HAVE_HW_NORMA_DMM
	&norma_dmm_driver_info,
#endif
#ifdef HAVE_HW_OLS
	&ols_driver_info,
#endif
#ifdef HAVE_HW_RIGOL_DS
	&rigol_ds_driver_info,
#endif
#ifdef HAVE_HW_SALEAE_LOGIC16
	&saleae_logic16_driver_info,
#endif
#ifdef HAVE_HW_TELEINFO
	&teleinfo_driver_info,
#endif
#ifdef HAVE_HW_TONDAJ_SL_814
	&tondaj_sl_814_driver_info,
#endif
#ifdef HAVE_HW_UNI_T_UT32X
	&uni_t_ut32x_driver_info,
#endif
#ifdef HAVE_HW_VICTOR_DMM
	&victor_dmm_driver_info,
#endif
#ifdef HAVE_HW_ZEROPLUS_LOGIC_CUBE
	&zeroplus_logic_cube_driver_info,
#endif
#ifdef HAVE_HW_ASIX_SIGMA
	&asix_sigma_driver_info,
#endif
#ifdef HAVE_HW_CHRONOVU_LA8
	&chronovu_la8_driver_info,
#endif
#ifdef HAVE_HW_LINK_MSO19
	&link_mso19_driver_info,
#endif
#ifdef HAVE_HW_ALSA
	&alsa_driver_info,
#endif
#ifdef HAVE_HW_FX2LAFW
	&fx2lafw_driver_info,
#endif
#ifdef HAVE_HW_HANTEK_DSO
	&hantek_dso_driver_info,
#endif
#ifdef HAVE_HW_AGILENT_DMM
	&agdmm_driver_info,
#endif
#ifdef HAVE_HW_FLUKE_DMM
	&flukedmm_driver_info,
#endif
#ifdef HAVE_HW_SERIAL_DMM
	&bbcgm_m2110_driver_info,
	&digitek_dt4000zc_driver_info,
	&tekpower_tp4000zc_driver_info,
	&metex_me31_driver_info,
	&peaktech_3410_driver_info,
	&mastech_mas345_driver_info,
	&va_va18b_driver_info,
	&va_va40b_driver_info,
	&metex_m3640d_driver_info,
	&metex_m4650cr_driver_info,
	&peaktech_4370_driver_info,
	&pce_pce_dm32_driver_info,
	&radioshack_22_168_driver_info,
	&radioshack_22_805_driver_info,
	&radioshack_22_812_driver_info,
	&tecpel_dmm_8061_ser_driver_info,
	&voltcraft_m3650d_driver_info,
	&voltcraft_m4650cr_driver_info,
	&voltcraft_vc820_ser_driver_info,
	&voltcraft_vc830_ser_driver_info,
	&voltcraft_vc840_ser_driver_info,
	&uni_t_ut60a_ser_driver_info,
	&uni_t_ut60e_ser_driver_info,
	&uni_t_ut61b_ser_driver_info,
	&uni_t_ut61c_ser_driver_info,
	&uni_t_ut61d_ser_driver_info,
	&uni_t_ut61e_ser_driver_info,
	&iso_tech_idm103n_driver_info,
#endif
#ifdef HAVE_HW_UNI_T_DMM
	&tecpel_dmm_8061_driver_info,
	&uni_t_ut60a_driver_info,
	&uni_t_ut60e_driver_info,
	&uni_t_ut61b_driver_info,
	&uni_t_ut61c_driver_info,
	&uni_t_ut61d_driver_info,
	&uni_t_ut61e_driver_info,
	&voltcraft_vc820_driver_info,
	&voltcraft_vc830_driver_info,
	&voltcraft_vc840_driver_info,
	&tenma_72_7745_driver_info,
#endif
	NULL,
};

/**
 * Return the list of supported hardware drivers.
 *
 * @return Pointer to the NULL-terminated list of hardware driver pointers.
 */
SR_API struct sr_dev_driver **sr_driver_list(void)
{

	return drivers_list;
}

/**
 * Initialize a hardware driver.
 *
 * This usually involves memory allocations and variable initializations
 * within the driver, but _not_ scanning for attached devices.
 * The API call sr_driver_scan() is used for that.
 *
 * @param ctx A libsigrok context object allocated by a previous call to
 *            sr_init(). Must not be NULL.
 * @param driver The driver to initialize. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid parameters,
 *         SR_ERR_BUG upon internal errors, or another negative error code
 *         upon other errors.
 */
SR_API int sr_driver_init(struct sr_context *ctx, struct sr_dev_driver *driver)
{
	int ret;

	if (!ctx) {
		sr_err("Invalid libsigrok context, can't initialize.");
		return SR_ERR_ARG;
	}

	if (!driver) {
		sr_err("Invalid driver, can't initialize.");
		return SR_ERR_ARG;
	}

	sr_spew("Initializing driver '%s'.", driver->name);
	if ((ret = driver->init(ctx)) < 0)
		sr_err("Failed to initialize the driver: %d.", ret);

	return ret;
}

/**
 * Tell a hardware driver to scan for devices.
 *
 * In addition to the detection, the devices that are found are also
 * initialized automatically. On some devices, this involves a firmware upload,
 * or other such measures.
 *
 * The order in which the system is scanned for devices is not specified. The
 * caller should not assume or rely on any specific order.
 *
 * Before calling sr_driver_scan(), the user must have previously initialized
 * the driver by calling sr_driver_init().
 *
 * @param driver The driver that should scan. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 * @param options A list of 'struct sr_hwopt' options to pass to the driver's
 *                scanner. Can be NULL/empty.
 *
 * @return A GSList * of 'struct sr_dev_inst', or NULL if no devices were
 *         found (or errors were encountered). This list must be freed by the
 *         caller using g_slist_free(), but without freeing the data pointed
 *         to in the list.
 */
SR_API GSList *sr_driver_scan(struct sr_dev_driver *driver, GSList *options)
{
	GSList *l;

	if (!driver) {
		sr_err("Invalid driver, can't scan for devices.");
		return NULL;
	}

	if (!driver->priv) {
		sr_err("Driver not initialized, can't scan for devices.");
		return NULL;
	}

	l = driver->scan(options);

	sr_spew("Scan of '%s' found %d devices.", driver->name,
		g_slist_length(l));

	return l;
}

/** @private */
SR_PRIV void sr_hw_cleanup_all(void)
{
	int i;
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list();
	for (i = 0; drivers[i]; i++) {
		if (drivers[i]->cleanup)
			drivers[i]->cleanup();
	}
}

/** A floating reference can be passed in for data.
 * @private */
SR_PRIV struct sr_config *sr_config_new(int key, GVariant *data)
{
	struct sr_config *src;

	if (!(src = g_try_malloc(sizeof(struct sr_config))))
		return NULL;
	src->key = key;
	src->data = g_variant_ref_sink(data);

	return src;
}

/** @private */
SR_PRIV void sr_config_free(struct sr_config *src)
{

	if (!src || !src->data) {
		sr_err("%s: invalid data!", __func__);
		return;
	}

	g_variant_unref(src->data);
	g_free(src);

}

/**
 * Returns information about the given driver or device instance.
 *
 * @param driver The sr_dev_driver struct to query.
 * @param sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 *            Otherwise it must be NULL.
 * @param probe_group The probe group on the device for which to list the
 *                    values, or NULL.
 * @param key The configuration key (SR_CONF_*).
 * @param data Pointer to a GVariant where the value will be stored. Must
 *             not be NULL. The caller is given ownership of the GVariant
 *             and must thus decrease the refcount after use. However if
 *             this function returns an error code, the field should be
 *             considered unused, and should not be unreferenced.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_API int sr_config_get(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group,
		int key, GVariant **data)
{
	int ret;

	if (!driver || !data)
		return SR_ERR;

	if (!driver->config_get)
		return SR_ERR_ARG;

	if ((ret = driver->config_get(key, data, sdi, probe_group)) == SR_OK) {
		/* Got a floating reference from the driver. Sink it here,
		 * caller will need to unref when done with it. */
		g_variant_ref_sink(*data);
	}

	return ret;
}

/**
 * Set a configuration key in a device instance.
 *
 * @param sdi The device instance.
 * @param probe_group The probe group on the device for which to list the
 *                    values, or NULL.
 * @param key The configuration key (SR_CONF_*).
 * @param data The new value for the key, as a GVariant with GVariantType
 *        appropriate to that key. A floating reference can be passed
 *        in; its refcount will be sunk and unreferenced after use.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_API int sr_config_set(const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group,
		int key, GVariant *data)
{
	int ret;

	g_variant_ref_sink(data);

	if (!sdi || !sdi->driver || !data)
		ret = SR_ERR;
	else if (!sdi->driver->config_set)
		ret = SR_ERR_ARG;
	else
		ret = sdi->driver->config_set(key, data, sdi, probe_group);

	g_variant_unref(data);

	return ret;
}

/**
 * List all possible values for a configuration key.
 *
 * @param driver The sr_dev_driver struct to query.
 * @param sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 * @param probe_group The probe group on the device for which to list the
 *                    values, or NULL.
 * @param key The configuration key (SR_CONF_*).
 * @param data A pointer to a GVariant where the list will be stored. The
 *             caller is given ownership of the GVariant and must thus
 *             unref the GVariant after use. However if this function
 *             returns an error code, the field should be considered
 *             unused, and should not be unreferenced.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that key,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_API int sr_config_list(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group,
		int key, GVariant **data)
{
	int ret;

	if (!driver || !data)
		ret = SR_ERR;
	else if (!driver->config_list)
		ret = SR_ERR_ARG;
	else if ((ret = driver->config_list(key, data, sdi, probe_group)) == SR_OK)
		g_variant_ref_sink(*data);

	return ret;
}

/**
 * Get information about a configuration key.
 *
 * @param key The configuration key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 */
SR_API const struct sr_config_info *sr_config_info_get(int key)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (sr_config_info_data[i].key == key)
			return &sr_config_info_data[i];
	}

	return NULL;
}

/**
 * Get information about an configuration key, by name.
 *
 * @param optname The configuration key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 */
SR_API const struct sr_config_info *sr_config_info_name_get(const char *optname)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (!strcmp(sr_config_info_data[i].id, optname))
			return &sr_config_info_data[i];
	}

	return NULL;
}

/* Unnecessary level of indirection follows. */

/** @private */
SR_PRIV int sr_source_remove(int fd)
{
	return sr_session_source_remove(fd);
}

/** @private */
SR_PRIV int sr_source_add(int fd, int events, int timeout,
			  sr_receive_data_callback_t cb, void *cb_data)
{
	return sr_session_source_add(fd, events, timeout, cb, cb_data);
}

/** @} */
