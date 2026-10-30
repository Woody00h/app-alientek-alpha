/* Adapted from driver/audio/audio_test.c, GPL-2.0. */
static void interval(struct snd_pcm_hw_params *p, unsigned int key,
		     unsigned int value)
{
	struct snd_interval *i = &p->intervals[key - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
	i->min = i->max = value;
	i->integer = 1;
}

static void mask(struct snd_pcm_hw_params *p, unsigned int key, unsigned int bit)
{
	struct snd_mask *m = &p->masks[key - SNDRV_PCM_HW_PARAM_FIRST_MASK];
	memset(m, 0, sizeof(*m));
	m->bits[bit / 32] = 1U << (bit % 32);
}

static int pcm_open(const char *path, int capture)
{
	struct snd_pcm_hw_params hw = {0};
	struct snd_pcm_sw_params sw = {0};
	int fd = open(path, O_RDWR | O_CLOEXEC);
	unsigned int i, j;
	int saved;

	if (fd < 0)
		return -1;
	for (i = 0; i < sizeof(hw.masks) / sizeof(hw.masks[0]); i++)
		for (j = 0; j < sizeof(hw.masks[i].bits) / sizeof(hw.masks[i].bits[0]); j++)
			hw.masks[i].bits[j] = UINT_MAX;
	for (i = 0; i < sizeof(hw.intervals) / sizeof(hw.intervals[0]); i++)
		hw.intervals[i].max = UINT_MAX;
	hw.rmask = UINT_MAX;
	mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
	mask(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	interval(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	interval(&hw, SNDRV_PCM_HW_PARAM_RATE, RATE);
	interval(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PERIOD);
	interval(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, BUFFER);
	if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0)
		goto fail;
	sw.period_step = 1;
	sw.avail_min = PERIOD;
	sw.xfer_align = 1;
	/* Queue three periods before playback starts (64 ms at 48 kHz). */
	sw.start_threshold = capture ? 1 : BUFFER - PERIOD;
	sw.stop_threshold = BUFFER;
	sw.boundary = BUFFER;
	while (sw.boundary <= (snd_pcm_uframes_t)LONG_MAX / 2 - BUFFER)
		sw.boundary *= 2;
	if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0 ||
	    ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0)
		goto fail;
	return fd;
fail:
	saved = errno;
	close(fd);
	errno = saved;
	return -1;
}

