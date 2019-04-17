### /dev/pcmCxDxc设备文件创建

#### snd_soc_register_card

```c
int snd_soc_register_card(struct snd_soc_card *card)
{
	/* 分配num_links + num_aux_devs个 struct snd_soc_pcm_runtime，并保存到card->rtd中 */
	card->rtd = devm_kzalloc(card->dev, sizeof(struct snd_soc_pcm_runtime) * 
				(card->num_links + card->num_aux_devs),GFP_KERNEL);

	card->num_rtd = 0;
	card->rtd_aux = &card->rtd[card->num_links];

	for (i = 0; i < card->num_links; i++) {
		card->rtd[i].card = card;
		card->rtd[i].dai_link = &card->dai_link[i];
		card->rtd[i].codec_dais = devm_kzalloc(card->dev,
						/* num_codecs为1 */
						sizeof(struct snd_soc_dai *) * 
                        (card->rtd[i].dai_link->num_codecs), GFP_KERNEL);
	}
	ret = snd_soc_instantiate_card(card);
}
```

#### snd_soc_instantiate_card

```c
static int snd_soc_instantiate_card(struct snd_soc_card *card)
{
	/* bind DAIs,为card->rtd绑定cpu_dai, codec_dais,platform */
	for (i = 0; i < card->num_links; i++) {
		ret = soc_bind_dai_link(card, i);
	}

	/* probe all DAI links on this card */
	for (order = SND_SOC_COMP_ORDER_FIRST; order <= SND_SOC_COMP_ORDER_LAST; order++)
	{
		for (i = 0; i < card->num_links; i++) {
			ret = soc_probe_link_dais(card, i, order);
		}
	}
	ret = snd_card_register(card->snd_card);
}
```

