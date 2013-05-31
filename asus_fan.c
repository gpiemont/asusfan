/*
 *  asus_fan.c - Advanced ACPI fan control
 *
 *
 *  Copyright (C) 2010-2013 Giulio Piemontese
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The development page for this driver is located at
 *
 *  http://github.com/gpiemont/asusfan/ 
 *
 *  Previusly (by Dmitry Ursegov)
 *  http://code.google.com/p/asusfan/
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("Giulio Piemontese");
MODULE_LICENSE("GPL");
MODULE_DESC("Advanced ACPI ASUS fan control");

#ifndef ASUSFAN_VERBOSE
#define ASUSFAN_VERBOSE 0
#endif

#ifndef ASUSFAN_STABLE_RANGE
#define	ASUSFAN_STABLE_RANGE	1
#endif

#ifndef ASUSFAN_TEMP_NUM_SAMPLES
#define	ASUSFAN_TEMP_NUM_SAMPLES 5
#endif

MODULE_PARM_DESC(verbose, "Enable Speed changing/temperature status messages (0-2)");
int asusfan_verbose = ACPIFAN_VERBOSE;
module_param_named(verbose, asusfan_verbose, int, 0600);

MODULE_PARM_DESC(current_zone, "Current thermal zone");
int asusfan_curr_zone = -1;
module_param_named(current_zone, asusfan_curr_zone, int, 0444);

MODULE_PARM_DESC(previous_zone, "Previous thermal zone");
int asusfan_prev_zone = -1;
module_param_named(previous_zone, asusfan_prev_speed, int, 0444);

MODULE_PARM_DESC(current_speed, "Current fan speed");
int asusfan_curr_speed = -1;
module_param_named(current_speed, asusfan_curr_speed, int, 0444);

MODULE_PARM_DESC(target_speed, "Manually set target fan speed (Dangerous!)");
int asusfan_target_speed = -1;
module_param_named(target_speed, asusfan_target_speed, int, 0400);

MODULE_PARM_DESC(max_speed, "Maximum supported speed");
int asusfan_max_speed = 255;
module_param_named(max_speed, asusfan_max_speed, int, 0400);

MODULE_PARM_DESC(min_speed, "Minumum supported speed");
int asusfan_min_speed = 0;
module_param_named(min_speed, asusfan_min_speed, int, 0400);

MODULE_PARM_DESC(current_temp, "Current temperature value (°C)");
int asusfan_curr_temp = -1;
module_param_named(current_temp, asusfan_curr_temp, int, 0444);

MODULE_PARM_DESC(max_temp, "Maximum temperature value (°C) that can be evaluated");
int asusfan_max_temp = 110;
module_param_named(max_temp, asusfan_max_temp, int, 0444);

MODULE_PARM_DESC(min_temp, "Minimum temperature value (°C) that can be evaluated");
int asusfan_min_temp = 0;
module_param_named(min_temp, asusfan_min_temp, int, 0444);

MODULE_PARM_DESC(temp_status, "Current temperature status");
static char asusfan_temp_status[11];
module_param_string(temp_status, asusfan_temp_status, 11 , 0444);

MODULE_PARM_DESC(temp_stable_range, "Within this range (+/-) temperature is considered stable");
int asusfan_stable_range = ASUSFAN_STABLE_RANGE;
module_param_named(temp_stable_range, asusfan_stable_range, int, 0600);

MODULE_PARM_DESC(temp_num_samples, "Number of samples to establish the temperature behaviour");
int asusfan_num_samples = 0;
module_param_named(temp_num_samples, asusfan_num_samples, int, 0600);

#if 0
/*
 * Acpi objects names to store/retrieve the fan speed and 
 * the temperature values, respectively.
 * Every platform will require its own method, provided
 * in the get_zone_temp()/set_fan_speed() routines.
 * Anyway we provide a convenience struct too, to store basic I/O object names.
 * I.e: read temperature from/store speed in. 
 * Ordered by acpi platform.
 *
 * XXX Find a way to determine platform/model via ACPI
 *
 */

struct acpi_tz_entry {
	const char *acpi_platform_name;
	const char *acpi_tz_obj;
	const char *acpi_target_obj;
	const char *acpi_current_speed_obj;	/* can be NULL */
};

static struct asus_tz_entries[] = {
	{ "default", "\\_TZ.THRM._TMP", "\\_SB.ATKD.ECRW", "\\_SB.PCI0.SBRG.EC0.CDT1" },
	NULL
};

int asus_acpi_platform_no =	0;	/* default platform */

#endif

/*
 * We took a pre-defined number of samples of temperature from the acpi,
 * then we try to guess the ongoing curve. This is done in a separated thread.
 */

unsigned int 	fan_sample = 0;

char 		fan_num_samples = ASUSFAN_TEMP_NUM_SAMPLES;
char		samples[ASUSFAN_TEMP_NUM_SAMPLES] ;

#define 	MONITOR_FREQ 1

/*
 * When the temperature goes down to a low zone it is better to stay at
 * a high speed some degrees more to reduce fan speed switching
 */

#define NUM_ZONES 	6
#define TMP_DIFF 	3

#define TIMER_FREQ 	5	/* seconds */

struct tmp_zone {
	int tmp;		/* °C */
	int speed;		/* 80-255 (0 - fan is disabled) */
	unsigned int sleep;	/* Sleep for sleep seconds more at tmp temperature */
};

int asusfan_current_zone = -1;

typedef enum status { 
	ascending, descending, stable 
} status_t;

status_t __thermal_status = stable;

static struct tmp_zone zone[NUM_ZONES] = {{ 40,	 80,  1 },
					  { 55,  110,  1 },
					  { 70,  130,  2 },
					  { 85,  160,  3 },
					  { 100, 190,  5 },
					  { 105, 210,  7 }};
 
static void timer_handler(struct work_struct *work);
static void temp_status_timer(struct work_struct *work);

static DECLARE_DELAYED_WORK(ws, timer_handler);
static DECLARE_DELAYED_WORK(wst, temp_status_timer);

static struct workqueue_struct *wqs;
static struct workqueue_struct *wqst;

static int get_zone_temp(void)
{
        struct acpi_buffer output;
        union acpi_object out_obj;
        acpi_status status;

        int tmp;

        output.length = sizeof(out_obj);
        output.pointer = &out_obj;

        //Get current temperature
        status = acpi_evaluate_object(NULL, "\\_TZ.THRM._TMP", 
                                        NULL, &output);
        if (status != AE_OK) printk("_TZ.THRM._TMP error\n");
        tmp = (int)((out_obj.integer.value-2732))/10;

	if((tmp > asusfan_max_temp ) || (tmp < asusfan_min_temp))
		return -1;

	if(asusfan_curr_temp != tmp)
		asusfan_curr_temp = tmp;

	return tmp;
}

static void set_fan_speed(int speed)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	acpi_status status;

#if 0
	/*
	 * It is required to evaluate this object with a maximum
	 * temperature supported by the manual control. If temperature is
	 * higher the manual control will be disabled
	 */

	params.count = 2;
	params.pointer = in_objs;
	in_objs[0].type = in_objs[1].type = ACPI_TYPE_INTEGER;
	in_objs[0].integer.value = zone[NUM_ZONES-1].tmp; //temp 
	in_objs[1].integer.value = 0;
	status = acpi_evaluate_object(NULL, "\\_TZ.THRM._WLM",
					NULL, NULL);
	if (status != AE_OK)
		printk("_TZ.THRM._CRT error\n");
#endif

	//Set fan speed	
	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = ((0x84 << 16)
				+ (speed << 8) + (0xc4));
	if(asusfan_verbose)	
		printk("acpi integer value (_SB.ATKD.ECRW ) : %llu\n", in_obj.integer.value);
	status = acpi_evaluate_object(NULL, "\\_SB.ATKD.ECRW",
					&params, NULL);
	if (status != AE_OK) printk("_SB.ATKD.ECRW error\n");
}

static void timer_handler(struct work_struct *work)
{
	static int prev_zone = 0;
	int curr_zone = 0;
	int tmp;

	tmp =  get_zone_temp();

	if (unlikely(tmp == -1))
		return;

	if(tmp >= zone[NUM_ZONES-1].tmp)
		goto out;

	//Set fan speed and save previous zone
	for(curr_zone=0; curr_zone<(NUM_ZONES-1); curr_zone++)
		if(tmp < zone[curr_zone].tmp)
			break;

	if(unlikely(curr_zone < prev_zone &&
			tmp > zone[curr_zone].tmp - TMP_DIFF  )) {
		set_fan_speed(zone[prev_zone].speed);
		asusfan_curr_speed = zone[prev_zone].speed;
		if (asusfan_verbose > 0)
			printk("tmp = %d, curr zone %d, prev zone %d, speed set to %d\n", 
						tmp, curr_zone, prev_zone, zone[prev_zone].speed);
		}
	else {
		set_fan_speed(zone[curr_zone].speed);
		asusfan_curr_speed = zone[curr_zone].speed;
		prev_zone = curr_zone;
		if (asusfan_verbose > 0)	
			printk("tmp = %d, curr zone %d, prev zone %d, speed set to %d\n", 
					tmp, curr_zone, prev_zone, zone[curr_zone].speed);
	}

out:
	
	queue_delayed_work(wqs, &ws, (TIMER_FREQ + zone[curr_zone].sleep) * HZ);
}

static void temp_status_timer(struct work_struct *work)
{
	int i = 0;
	int diff = 0;

	samples[0] = get_zone_temp();

	/* "Parabolic" tolerance curve */

	if ((fan_sample % ASUSFAN_TEMP_NUM_SAMPLES) == 0)
	{
		diff = (samples[0] - samples[fan_num_samples - 1]);

		if(asusfan_verbose > 1)
			printk("sample[0] = %d , sample[%d] = %d, diff = %d\n", samples[0], fan_num_samples - 1, samples[fan_num_samples-1], diff);

		if((diff == 0 ) || ((diff <= asusfan_stable_range) && ( diff >= -asusfan_stable_range)))
		{
			__thermal_status = stable;
			snprintf(asusfan_temp_status, 11, "stable");
		}
		else if(diff > asusfan_stable_range )
		{
			__thermal_status = ascending;
			snprintf(asusfan_temp_status, 11, "ascending");
		}
		else if(diff < -asusfan_stable_range )
		{
			__thermal_status = descending;
			snprintf(asusfan_temp_status, 11, "descending");
		}
		if(asusfan_verbose > 1)
			printk("sample[0] = %d ", samples[0]);

		while(++i < fan_num_samples) {
			samples[fan_num_samples - i] = samples[fan_num_samples - i -1];
			if(asusfan_verbose > 1)
				printk("sample[%d] = %d ", i, samples[i]);
		}
		if(asusfan_verbose > 1)
			printk("\n");
	}

	queue_delayed_work(wqst, &wst, MONITOR_FREQ * HZ);
}

static int asus_fan_init(void)
{
	
	memset(&samples, 0, fan_num_samples);
	
	//Workqueue settings
	wqs = create_singlethread_workqueue("tmp");
	queue_delayed_work(wqs, &ws, HZ);

	wqst = create_singlethread_workqueue("sampler");
	queue_delayed_work(wqst, &wst, HZ);

	printk("Asus Advanced Fan Control version 0.7\n");

	return 0;
}

static void asus_fan_exit(void)
{
	cancel_delayed_work(&ws);
	flush_workqueue(wqs);
	destroy_workqueue(wqs);

	cancel_delayed_work(&wst);
	flush_workqueue(wqst);
	destroy_workqueue(wqst);
	
	printk("Asus Fan Control driver unloaded\n");
}


module_init(asus_fan_init);
module_exit(asus_fan_exit);

