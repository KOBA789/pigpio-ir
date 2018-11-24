#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <pigpio.h>

const double FREQ = 38.0;
const uint32_t T = 425;
const double DUTY = 0.333;
const int PIN = 18;
const size_t LEADER = 8;
const size_t INTERVAL = 4;
const size_t STOPBIT = 1;
const size_t LOOP_BEGIN = 2;
const size_t LOOP_END = 4;

void show_usage(void) {
	fprintf(stderr, "usage: irtx [<repeat> <interval>] <data>\n");
}

int parse_arg(const int args, const char *const *const argv, uint8_t *repeat, uint16_t *interval, const char **const hex) {
	long int tmp;
	char *endptr;

	switch (args) {
	case 1:
		*repeat = 1;
		*interval = 0;
		*hex = argv[0];
		break;
	case 3:
		errno = 0;
		tmp = strtol(argv[0], &endptr, 10);
		if (errno != 0 || *endptr != '\0' || tmp < 0 || tmp > (long int)UINT8_MAX) {
			return -1;
		}
		*repeat = (uint8_t)tmp;

		tmp = strtol(argv[1], &endptr, 10);
		if (errno != 0 || *endptr != '\0' || tmp < 0 || tmp > (long int)UINT16_MAX) {
			return -1;
		}
		*interval = (uint16_t)tmp;
		*hex = argv[2];
		break;
	default:
		return -1;
	}
	return 0;
}

int8_t parse_hex(char chr) {
	if ('0' <= chr && chr <= '9') {
		return (int8_t)(chr - '0');
	}
	if ('a' <= chr && chr <= 'f') {
		return (int8_t)(chr - 'a') + 10;
	}
	if ('A' <= chr && chr <= 'F') {
		return (int8_t)(chr - 'A') + 10;
	}
	return -1;
}

int parse_data(const char *const hex, uint8_t *const data, const size_t len) {
	int i;
	char chr;
	int8_t tmp;

	for (i = 0; i < (int)len; i++) {
		chr = hex[i * 2];
		tmp = parse_hex(chr);
		if (tmp < 0) {
			return -1;
		}
		data[i] = tmp << 4;
		chr = hex[i * 2 + 1];
		tmp = parse_hex(chr);
		if (tmp < 0) {
			return -1;
		}
		data[i] += tmp;
	}
	return i;
}

void generate_waveform(const double cycle, const int nc, gpioPulse_t *const pulses) {
	int c;
	uint32_t on, off, sofar, target;

	on = (uint32_t)round(cycle * DUTY);
	sofar = 0;
	for (c = 0; c < nc; c++) {
		target = (uint32_t)round((c + 1) * cycle);
		sofar += on;
		off = target - sofar;
		sofar += off;

		pulses[c * 2].gpioOn = 1 << PIN;
		pulses[c * 2].gpioOff = 0;
		pulses[c * 2].usDelay = on;

		pulses[c * 2 + 1].gpioOn = 0;
		pulses[c * 2 + 1].gpioOff = 1 << PIN;
		pulses[c * 2 + 1].usDelay = off;
	}
}

int transmit(const uint8_t *const data, const size_t len, const uint8_t repeat, const uint16_t interval) {
	double cycle;
	unsigned int i, k, p, nc, np;
	gpioPulse_t *wf;
	size_t chain_len;
	char *chain;
	int zero, one, high, low4;

	if (gpioInitialise() < 0) {
		fprintf(stderr, "failed to init GPIO\n");
		return -1;
	}
	if (gpioSetMode(PIN, PI_OUTPUT) != 0) {
		gpioTerminate();
		return -1;
	}
	if (gpioWaveClear() != 0) {
		gpioTerminate();
		return -1;
	}


	cycle = 1000 / FREQ;
	nc = (int)round((double)T / cycle);
	np = nc * 2 + 1;

	wf = (gpioPulse_t *)malloc(sizeof(gpioPulse_t) * np);
	if (wf == NULL) {
		fprintf(stderr, "could not alloc pulse\n");
		gpioTerminate();
		return -1;
	}
	generate_waveform(cycle, nc, wf);

	wf[np - 1].gpioOn = 0;
	wf[np - 1].gpioOff = 1 << PIN;
	wf[np - 1].usDelay = T;
	if (gpioWaveAddGeneric(np, wf) < 0) {
		fprintf(stderr, "could not add wave form of zero\n");
		gpioTerminate();
		return -1;
	}
	if ((zero = gpioWaveCreate()) < 0) {
		fprintf(stderr, "could not register wave form of zero\n");
		gpioTerminate();
		return -1;
	}

	wf[np - 1].usDelay = 3 * T;
	if (gpioWaveAddGeneric(np, wf) < 0) {
		fprintf(stderr, "could not add wave form of one\n");
		gpioTerminate();
		return -1;
	}
	if ((one = gpioWaveCreate()) < 0) {
		fprintf(stderr, "could not register wave form of one\n");
		gpioTerminate();
		return -1;
	}

	if (gpioWaveAddGeneric(np - 1, wf) < 0) {
		gpioTerminate();
		return -1;
	}
	if ((high = gpioWaveCreate()) < 0) {
		gpioTerminate();
		return -1;
	}

	wf[0].gpioOn = 0;
	wf[0].gpioOff = 1 << PIN;
	wf[0].usDelay = 4 * T;
	if (gpioWaveAddGeneric(1, wf) < 0) {
		gpioTerminate();
		return -1;
	}
	if ((low4 = gpioWaveCreate()) < 0) {
		gpioTerminate();
		return -1;
	}

	chain_len = (LEADER + INTERVAL + STOPBIT + len * 8) + LOOP_BEGIN + LOOP_END;
	chain = (char *)malloc(sizeof(char) * chain_len);
	if (chain == NULL) {
		gpioTerminate();
		return -1;
	}

	p = 0;
	/* LOOP BEGIN */
	chain[p++] = 255;
	chain[p++] = 0;
	/* /LOOP BEGIN */
	/* LEADER */
	chain[p++] = 255; // loop begin
	chain[p++] = 0;

	chain[p++] = high;

	chain[p++] = 255; // loop end
	chain[p++] = 1;   //   repeat 8 times
	chain[p++] = 8;
	chain[p++] = 0;

	chain[p++] = low4;
	/* /LEADER */
	for (i = 0; i < len; i++) {
		for (k = 0; k < 8; k++) {
			if (((data[i] >> k) & 1) == 1) {
				chain[p++] = one;
			} else {
				chain[p++] = zero;
			}
		}
	}
	/* STOPBIT */
	chain[p++] = zero;
	/* /STOPBIT */
	/* INTERVAL */
	chain[p++] = 255;
	chain[p++] = 2;
	chain[p++] = (char)(interval & 0xff);
	chain[p++] = (char)((interval >> 4) & 0xff);
	/* /INTERVAL */
	/* LOOP_END */
	chain[p++] = 255;
	chain[p++] = 1;
	chain[p++] = (char)(repeat & 0xff);
	chain[p++] = (char)((repeat >> 4) & 0xff);
	/* /LOOP_END */

	if (gpioWaveChain(chain, chain_len) != 0) {
		gpioTerminate();
		return -1;
	}

	while (gpioWaveTxBusy())
	{
		time_sleep(0.1);
	}

	gpioWrite(PIN, 0);

	gpioTerminate();

	return 0;
}

int main(const int args, const char *const *const argv) {
	uint8_t repeat;
	uint16_t interval;
	const char *hex;
	uint8_t *data;
	size_t len;

	if (parse_arg(args - 1, &argv[1], &repeat, &interval, &hex) != 0) {
		fprintf(stderr, "invalid argument\n");
		show_usage();
		exit(EXIT_FAILURE);
	}

	len = strlen(hex);
	if (len % 2 != 0) {
		fprintf(stderr, "data must be aligned with bytes\n");
		exit(EXIT_FAILURE);
	}
	len /= 2;
	data = (uint8_t *)malloc(sizeof(uint8_t) * len);
	if (data == NULL) {
		exit(EXIT_FAILURE);
	}
	if (parse_data(hex, data, len) < 0) {
		fprintf(stderr, "data must be in hex\n");
		exit(EXIT_FAILURE);
	}

	if (transmit(data, len, repeat, interval) < 0) {
		fprintf(stderr, "failed to transmit\n");
		exit(EXIT_FAILURE);
	}


	return EXIT_SUCCESS;
}
