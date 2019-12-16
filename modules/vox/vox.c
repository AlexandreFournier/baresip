/**
 * @file vox.c  Voice-operated switch (VOX)
 *
 * Copyright (C) 2019 Creytiv.com
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <wiringPi.h>

/**
 * @defgroup vox vox
 *
 * Simple Voice-operated switch (VOX) for HAM radio applications
 *
 * The VOX module takes the audio-signal as input and detects voice activity.
 * It enables the PTT GPIO when voice activity is detected. If configured, the
 * the Squelch GPIO can be use to preempt the PTT GPIO. It is using the aufilt
 * API to get the audio samples.
 */


struct vox_dec {
	struct aufilt_dec_st af;  /* inheritance */
	struct tmr tmr;
	const struct audio *au;
	double avg_play;
	volatile bool started;
	enum aufmt fmt;
};

#define UPDATE_PERIOD 100 // ms

static int  vox_threshold    =   60; /* Voice level threshold (-dBov) */
static int  vox_holdtime     = 1000; /* Push-to-talk hold time (ms) */
static int  vox_gpio_ptt     =   -1; /* Push-to-talk GPIO (out) */
static int  vox_gpio_squelch =   -1; /* Squelch GPIO (in) */

static int  ptt_release      = 0;

static bool squelch(void)
{
	if (-1 != vox_gpio_squelch) {
		return digitalRead(vox_gpio_squelch);
	} else {
		return 0;
	}
}


static void ptt_set(bool value)
{
	static bool current = 0;
	if (value == current)
		return;

	if (-1 != vox_gpio_ptt) {
		digitalWrite(vox_gpio_ptt, value ? HIGH : LOW);
	}

	current = value;
}


static void vox_update(double value)
{
	if (squelch()) { 
		ptt_release = 0;
	} else if (value > -vox_threshold) {
		ptt_release = vox_holdtime / UPDATE_PERIOD;
	}

	if (ptt_release > 0) {
		ptt_set(1);
		ptt_release--;
	} else {
		ptt_set(0);
	}
}


static void dec_destructor(void *arg)
{
	struct vox_dec *st = arg;

	list_unlink(&st->af.le);
	tmr_cancel(&st->tmr);
}


static void dec_tmr_handler(void *arg)
{
	struct vox_dec *st = arg;

	tmr_start(&st->tmr, UPDATE_PERIOD, dec_tmr_handler, st);

	if (st->started) {
		vox_update(st->avg_play);
	}
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct vox_dec *st;
	(void)ctx;
	(void)prm;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->au = au;
	st->fmt = prm->fmt;
	tmr_start(&st->tmr, UPDATE_PERIOD, dec_tmr_handler, st);

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static int decode(struct aufilt_dec_st *st, void *sampv, size_t *sampc)
{
	struct vox_dec *vu = (void *)st;

	if (!st || !sampv || !sampc)
		return EINVAL;

	vu->avg_play = aulevel_calc_dbov(vu->fmt, sampv, *sampc);
	vu->started = true;

	return 0;
}


static struct aufilt vox = {
	.name    = "vox",
	.decupdh = decode_update,
	.dech    = decode
};


static int module_init(void)
{
	struct conf *conf = conf_cur();
	uint32_t v;

	if (0 == conf_get_u32(conf, "vox_threshold", &v))
		vox_threshold = v;
	if (0 == conf_get_u32(conf, "vox_holdtime", &v))
		vox_holdtime = v;
	if (0 == conf_get_u32(conf, "vox_gpio_ptt", &v))
		vox_gpio_ptt = v;
	if (0 == conf_get_u32(conf, "vox_gpio_squelch", &v))
		vox_gpio_squelch = v;

	debug("Loading VOX module treshold=%d holdtime=%d gpio_ptt=%d gpio_squelch=%d\n",
		vox_threshold, vox_holdtime,
		vox_gpio_ptt, vox_gpio_squelch);

	aufilt_register(baresip_aufiltl(), &vox);

	wiringPiSetup();
	if (vox_gpio_ptt != -1) {
		pinMode(vox_gpio_ptt, OUTPUT);
	}
	if (vox_gpio_squelch != -1) {
		pinMode(vox_gpio_squelch, INPUT);
		pullUpDnControl(vox_gpio_squelch, PUD_DOWN);
	}

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&vox);
	ptt_set(0);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vox) = {
	"vox",
	"filter",
	module_init,
	module_close
};
