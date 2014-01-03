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

#include <stdio.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "device"

/**
 * @file
 *
 * Device handling in libsigrok.
 */

/**
 * @defgroup grp_devices Devices
 *
 * Device handling in libsigrok.
 *
 * @{
 */

/** @private
 *  Allocate and initialize new struct sr_probe
 *  @param[in]  index @copydoc sr_probe::index
 *  @param[in]  type @copydoc sr_probe::type
 *  @param[in]  enabled @copydoc sr_probe::enabled
 *  @param[in]  name @copydoc sr_probe::name
 *
 *  @return NULL (failure) or new struct sr_probe*.
 */
SR_PRIV struct sr_probe *sr_probe_new(int index, int type,
		gboolean enabled, const char *name)
{
	struct sr_probe *probe;

	if (!(probe = g_try_malloc0(sizeof(struct sr_probe)))) {
		sr_err("Probe malloc failed.");
		return NULL;
	}

	probe->index = index;
	probe->type = type;
	probe->enabled = enabled;
	if (name)
		probe->name = g_strdup(name);

	return probe;
}

/**
 * Set the name of the specified probe in the specified device.
 *
 * If the probe already has a different name assigned to it, it will be
 * removed, and the new name will be saved instead.
 *
 * @param sdi The device instance the probe is connected to.
 * @param[in] probenum The number of the probe whose name to set.
 *                 Note that the probe numbers start at 0.
 * @param[in] name The new name that the specified probe should get. A copy
 *             of the string is made.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_probe_name_set(const struct sr_dev_inst *sdi,
		int probenum, const char *name)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			g_free(probe->name);
			probe->name = g_strdup(name);
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Enable or disable a probe on the specified device.
 *
 * @param sdi The device instance the probe is connected to.
 * @param probenum The probe number, starting from 0.
 * @param state TRUE to enable the probe, FALSE to disable.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_probe_enable(const struct sr_dev_inst *sdi, int probenum,
		gboolean state)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			probe->enabled = state;
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Add a trigger to the specified device (and the specified probe).
 *
 * If the specified probe of this device already has a trigger, it will
 * be silently replaced.
 *
 * @param[in,out] sdi Pointer to the device instance; must not be NULL.
 * @param[in] probenum Number of probe, starting at 0.
 * @param[in] trigger Trigger string, in the format used by sigrok-cli
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_trigger_set(const struct sr_dev_inst *sdi, int probenum,
		const char *trigger)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			/* If the probe already has a trigger, kill it first. */
			g_free(probe->trigger);
			probe->trigger = g_strdup(trigger);
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Determine whether the specified device instance has the specified
 * capability.
 *
 * @param sdi Pointer to the device instance to be checked. Must not be NULL.
 *            If the device's 'driver' field is NULL (virtual device), this
 *            function will always return FALSE (virtual devices don't have
 *            a hardware capabilities list).
 * @param[in] key The option that should be checked for is supported by the
 *            specified device.
 *
 * @retval TRUE Device has the specified option
 * @retval FALSE Device does not have the specified option, invalid input
 *         parameters or other error conditions.
 *
 * @since 0.2.0
 */
SR_API gboolean sr_dev_has_option(const struct sr_dev_inst *sdi, int key)
{
	GVariant *gvar;
	const int *devopts;
	gsize num_opts, i;
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->config_list)
		return FALSE;

	if (sdi->driver->config_list(SR_CONF_DEVICE_OPTIONS,
				&gvar, NULL, NULL) != SR_OK)
		return FALSE;

	ret = FALSE;
	devopts = g_variant_get_fixed_array(gvar, &num_opts, sizeof(int32_t));
	for (i = 0; i < num_opts; i++) {
		if (devopts[i] == key) {
			ret = TRUE;
			break;
		}
	}
	g_variant_unref(gvar);

	return ret;
}

/** @private
 *  Allocate and init new device instance struct.
 *  @param[in]  index   @copydoc sr_dev_inst::index
 *  @param[in]  status  @copydoc sr_dev_inst::status
 *  @param[in]  vendor  @copydoc sr_dev_inst::vendor
 *  @param[in]  model   @copydoc sr_dev_inst::model
 *  @param[in]  version @copydoc sr_dev_inst::version
 *
 *  @retval NULL Error
 *  @retval struct sr_dev_inst *. Dynamically allocated, free using
 *              sr_dev_inst_free().
 */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = g_try_malloc(sizeof(struct sr_dev_inst)))) {
		sr_err("Device instance malloc failed.");
		return NULL;
	}

	sdi->driver = NULL;
	sdi->index = index;
	sdi->status = status;
	sdi->inst_type = -1;
	sdi->vendor = vendor ? g_strdup(vendor) : NULL;
	sdi->model = model ? g_strdup(model) : NULL;
	sdi->version = version ? g_strdup(version) : NULL;
	sdi->probes = NULL;
	sdi->probe_groups = NULL;
	sdi->conn = NULL;
	sdi->priv = NULL;

	return sdi;
}

/** @private
 *  Free device instance struct created by sr_dev_inst().
 *  @param sdi  struct* to free.
 */
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	struct sr_probe *probe;
	GSList *l;

	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		g_free(probe->name);
		g_free(probe->trigger);
		g_free(probe);
	}
	g_slist_free(sdi->probes);

	if (sdi->probe_groups)
		g_slist_free(sdi->probe_groups);

	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi);
}

#ifdef HAVE_LIBUSB_1_0

/** @private
 *  Allocate and init struct for USB device instance.
 *  @param[in]  bus @copydoc sr_usb_dev_inst::bus
 *  @param[in]  address @copydoc sr_usb_dev_inst::address
 *  @param[in]  hdl @copydoc sr_usb_dev_inst::devhdl
 *
 *  @retval NULL Error
 *  @retval other struct sr_usb_dev_inst * for USB device instance.
 */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	if (!(udi = g_try_malloc(sizeof(struct sr_usb_dev_inst)))) {
		sr_err("USB device instance malloc failed.");
		return NULL;
	}

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl;

	return udi;
}

/** @private
 *  Free struct * allocated by sr_usb_dev_inst().
 *  @param usb  struct* to free. Must not be NULL.
 */
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	g_free(usb);
}

#endif

#ifdef HAVE_LIBSERIALPORT

/**
 * @private
 *
 * Both parameters are copied to newly allocated strings, and freed
 * automatically by sr_serial_dev_inst_free().
 *
 * @param[in] port OS-specific serial port specification. Examples:
 *                 "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/tty.Modem-0", "COM1".
 * @param[in] serialcomm A serial communication parameters string, in the form
 *              of \<speed\>/\<data bits\>\<parity\>\<stopbits\>, for example
 *              "9600/8n1" or "600/7o2". This is an optional parameter;
 *              it may be filled in later.
 *
 * @return A pointer to a newly initialized struct sr_serial_dev_inst,
 *         or NULL on error.
 */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;

	if (!port) {
		sr_err("Serial port required.");
		return NULL;
	}

	if (!(serial = g_try_malloc0(sizeof(struct sr_serial_dev_inst)))) {
		sr_err("Serial device instance malloc failed.");
		return NULL;
	}

	serial->port = g_strdup(port);
	if (serialcomm)
		serial->serialcomm = g_strdup(serialcomm);

	return serial;
}

/** @private
 *  Free struct sr_serial_dev_inst * allocated by sr_serial_dev_inst().
 *  @param serial   struct sr_serial_dev_inst * to free. Must not be NULL.
 */
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{
	g_free(serial->port);
	g_free(serial->serialcomm);
	g_free(serial);
}
#endif

/** @private */
SR_PRIV struct sr_usbtmc_dev_inst *sr_usbtmc_dev_inst_new(const char *device)
{
	struct sr_usbtmc_dev_inst *usbtmc;

	if (!device) {
		sr_err("Device name required.");
		return NULL;
	}

	if (!(usbtmc = g_try_malloc0(sizeof(struct sr_usbtmc_dev_inst)))) {
		sr_err("USBTMC device instance malloc failed.");
		return NULL;
	}

	usbtmc->device = g_strdup(device);
	usbtmc->fd = -1;

	return usbtmc;
}

/** @private */
SR_PRIV void sr_usbtmc_dev_inst_free(struct sr_usbtmc_dev_inst *usbtmc)
{
	g_free(usbtmc->device);
	g_free(usbtmc);
}

/**
 * Get the list of devices/instances of the specified driver.
 *
 * @param driver The driver to use. Must not be NULL.
 *
 * @return The list of devices/instances of this driver, or NULL upon errors
 *         or if the list is empty.
 *
 * @since 0.2.0
 */
SR_API GSList *sr_dev_list(const struct sr_dev_driver *driver)
{
	if (driver && driver->dev_list)
		return driver->dev_list();
	else
		return NULL;
}

/**
 * Clear all devices/instances of the specified driver.
 *
 * @param driver The driver to use. Must not be NULL.
 *
 * @return SR_OK upon success, a negative error code upon errors.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_clear(const struct sr_dev_driver *driver)
{
	if (driver && driver->dev_clear)
		return driver->dev_clear();
	else
		return SR_OK;
}

/**
 * Open the specified device.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return SR_OK upon success, a negative error code upon errors.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->dev_open)
		return SR_ERR;

	ret = sdi->driver->dev_open(sdi);

	return ret;
}

/**
 * Close the specified device.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return SR_OK upon success, a negative error code upon errors.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_close(struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->dev_close)
		return SR_ERR;

	ret = sdi->driver->dev_close(sdi);

	return ret;
}

/** @} */
