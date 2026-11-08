/* 자동 생성: tools/genchan_rtw8821c.py (원본 Linux rtw88, BSD-3-Clause 로 사용. Copyright(c) 2018-2019 Realtek Corporation) */
#define BCN_PRI_EN 0x1
#define BIT_BCN_QUEUE BIT(3)
#define BIT_BT_PTA_EN BIT(5)
#define BIT_EN_BCN_FUNCTION BIT(3)
#define BIT_FEN_EN BIT(26)
#define BIT_GNT_BT_POLARITY BIT(12)
#define BIT_MASK_SAMPLE_RATE GENMASK(5, 0)
#define BIT_PO_BT_PTA_PINS BIT(9)
#define BIT_PTA_EDCCA_EN BIT(5)
#define BIT_PTA_WL_TX_EN BIT(4)
#define BIT_WL_RST BIT(16)
#define BTG_CCA 0x0e
#define BTG_LNA 0xfc84
#define BT_CNT_ENABLE 0x1
#define B_BTG_SWITCH BIT(16)
#define B_CTRL_SWITCH BIT(18)
#define B_WLA_SWITCH BIT(23)
#define B_WLG_SWITCH BIT(21)
#define B_WL_SWITCH (BIT(20) | BIT(22))
#define REG_ACBB0 0x948
#define REG_ACBBRXFIR 0x94c
#define REG_ADC160 0x08C4
#define REG_ADC40 0x8c8
#define REG_ADCCLK 0x08AC
#define REG_BCN_CTRL 0x0550
#define REG_BT_COEX_TABLE_H 0x06CC
#define REG_BT_COEX_V2 0x0762
#define REG_BT_STAT_CTRL 0x0778
#define REG_BT_TDMA_TIME 0x0790
#define REG_CCA_FLTR 0xa20
#define REG_CHFIR 0x8f0
#define REG_CLKTRK 0x0860
#define REG_DMEM_CTRL 0x1080
#define REG_ENRXCCA 0xa84
#define REG_ENTXCCK 0xa80
#define REG_GPIO_MUXCFG 0x0040
#define REG_QUEUE_CTRL 0x04C6
#define REG_RFECTL 0xcb8
#define REG_RXCCAMSK 0x814
#define REG_RXPSEL 0x0808
#define REG_RXSB 0x0a00
#define REG_SYS_CTRL 0x000
#define REG_TXDFIR 0xc20
#define REG_TXFILTER 0xaac
#define REG_TXSCALE_A 0x0c1c
#define REG_TXSF2 0xa24
#define REG_TXSF6 0xa28
#define RF18_BW_20M_21 (BIT(11) | BIT(10))
#define RF18_BW_40M_21 (BIT(11))
#define RF18_BW_80M_21 (BIT(10))
#define RF18_BW_MASK_21 (BIT(11) | BIT(10))
#define RF18_RFSI_GE (BIT(17))
#define RF18_RFSI_GT (BIT(18))
#define RF_LUTDBG 0xdf
#define RF_XTALX2 0xb8
#define WLG_CCA 0x12
#define WLG_LNA 0x7532
#define BIT_CTRL_TYPE1 BIT(5)
#define BIT_CTRL_TYPE2 BIT(4)
#define BIT_DPDT_SEL_EN BIT(23)
#define BIT_DPDT_WL_SEL BIT(24)
#define BIT_MASK_RFE_SEL89 GENMASK(7, 0)
#define BIT_MASK_R_RFE_SEL_15 GENMASK(31, 28)
#define DPDT_CTRL_PIN 0x77
#define REG_CTRL_TYPE 0x67
#define REG_LED_CFG 0x004C
#define REG_RFE_CTRL8 0x0cb4

enum rtw8821ce_rf_set {
	SWITCH_TO_BTG,
	SWITCH_TO_WLG,
	SWITCH_TO_WLA,
	SWITCH_TO_BT,
};

static void rtw8821c_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT nterrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, BT_CNT_ENABLE);

	/* enable PTA (3-wire function form BT side) */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);

	/* enable PTA (tx/rx signal form WiFi side) */
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);
	/* wl tx signal to PTA not case EDCCA */
	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT_PTA_EDCCA_EN);
	/* GNT_BT=1 while select both */
	rtw_write16_set(rtwdev, REG_BT_COEX_V2, BIT_GNT_BT_POLARITY);

	/* beacon queue always hi-pri  */
	rtw_write8_mask(rtwdev, REG_BT_COEX_TABLE_H + 3, BIT_BCN_QUEUE,
			BCN_PRI_EN);
}

static void rtw8821c_switch_rf_set(struct rtw_dev *rtwdev, u8 rf_set)
{
	u32 reg;

	rtw_write32_set(rtwdev, REG_DMEM_CTRL, BIT_WL_RST);
	rtw_write32_set(rtwdev, REG_SYS_CTRL, BIT_FEN_EN);

	reg = rtw_read32(rtwdev, REG_RFECTL);
	switch (rf_set) {
	case SWITCH_TO_BTG:
		reg |= B_BTG_SWITCH;
		reg &= ~(B_CTRL_SWITCH | B_WL_SWITCH | B_WLG_SWITCH |
			 B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, BTG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, BTG_LNA);
		break;
	case SWITCH_TO_WLG:
		reg |= B_WL_SWITCH | B_WLG_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, WLG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, WLG_LNA);
		break;
	case SWITCH_TO_WLA:
		reg |= B_WL_SWITCH | B_WLA_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLG_SWITCH);
		break;
	case SWITCH_TO_BT:
	default:
		break;
	}

	rtw_write32(rtwdev, REG_RFECTL, reg);
}

static void rtw8821c_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 rf_reg18;

	rf_reg18 = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	rf_reg18 &= ~(RF18_BAND_MASK | RF18_CHANNEL_MASK | RF18_RFSI_MASK |
		      RF18_BW_MASK_21);

	rf_reg18 |= (channel <= 14 ? RF18_BAND_2G : RF18_BAND_5G);
	rf_reg18 |= (channel & RF18_CHANNEL_MASK);

	if (channel >= 100 && channel <= 140)
		rf_reg18 |= RF18_RFSI_GE;
	else if (channel > 140)
		rf_reg18 |= RF18_RFSI_GT;

	switch (bw) {
	case RTW_CHANNEL_WIDTH_5:
	case RTW_CHANNEL_WIDTH_10:
	case RTW_CHANNEL_WIDTH_20:
	default:
		rf_reg18 |= RF18_BW_20M_21;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_reg18 |= RF18_BW_40M_21;
		break;
	case RTW_CHANNEL_WIDTH_80:
		rf_reg18 |= RF18_BW_80M_21;
		break;
	}

	if (channel <= 14) {
		if (hal->rfe_btg)
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_BTG);
		else
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLG);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x1);
		rtw_write_rf(rtwdev, RF_PATH_A, 0x64, 0xf, 0xf);
	} else {
		rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLA);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x0);
	}

	rtw_write_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK, rf_reg18);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 1);
}

static void rtw8821c_set_channel_rxdfir(struct rtw_dev *rtwdev, u8 bw)
{
	if (bw == RTW_CHANNEL_WIDTH_40) {
		/* RX DFIR for BW40 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	} else if (bw == RTW_CHANNEL_WIDTH_80) {
		/* RX DFIR for BW80 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x1);
	} else {
		/* RX DFIR for BW20, BW10 and BW5 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	}
}

static void rtw8821c_cck_tx_filter_srrc(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (channel == 14) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else if (channel == 13 ||
		   (channel == 11 && bw == RTW_CHANNEL_WIDTH_40)) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xf8fe);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x64b80c1c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x8810);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x01235667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00029);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00026);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
				 hal->ch_param[0]);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
				 hal->ch_param[1] & MASKLWORD);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
				 hal->ch_param[2]);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	}
}

static void rtw8821c_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 val32;

	if (channel <= 14) {
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x0);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x0);
		rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x96a);

		if (rtw_regd_srrc(rtwdev)) {
			rtw8821c_cck_tx_filter_srrc(rtwdev, channel, bw);
			goto set_bw;
		}

		/* CCK TX filter parameters for default case */
		if (channel == 14) {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);
		} else {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
					 hal->ch_param[0]);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
					 hal->ch_param[1] & MASKLWORD);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
					 hal->ch_param[2]);
		}
	} else if (channel > 35) {
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		if (channel >= 36 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x1);
		else if (channel >= 100 && channel <= 144)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x2);
		else if (channel >= 149)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x3);

		if (channel >= 36 && channel <= 48)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x494);
		else if (channel >= 52 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x453);
		else if (channel >= 100 && channel <= 116)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x452);
		else if (channel >= 118 && channel <= 177)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x412);
	}

set_bw:
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
	default:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xffcffc00;
		val32 |= 0x10010000;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_40:
		if (primary_ch_idx == 1)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));

		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xff3ff300;
		val32 |= 0x20020000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_40;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_80:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xfcffcf00;
		val32 |= 0x40040000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_80;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_5:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x200240;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_10:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x300380;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	}
}

static u32 rtw8821c_get_bb_swing(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_efuse efuse = rtwdev->efuse;
	u8 tx_bb_swing;
	u32 swing2setting[4] = {0x200, 0x16a, 0x101, 0x0b6};

	tx_bb_swing = channel <= 14 ? efuse.tx_bb_swing_setting_2g :
				      efuse.tx_bb_swing_setting_5g;
	if (tx_bb_swing > 9)
		tx_bb_swing = 0;

	return swing2setting[(tx_bb_swing / 3)];
}

static void rtw8821c_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 channel,
					  u8 bw, u8 primary_ch_idx)
{
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, GENMASK(31, 21),
			 rtw8821c_get_bb_swing(rtwdev, channel));
	rtw8821c_pwrtrack_init(rtwdev);
}

static void rtw8821c_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8821c_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_bb_swing(rtwdev, channel, bw, primary_chan_idx);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_rf(rtwdev, channel, bw);
	rtw8821c_set_channel_rxdfir(rtwdev, bw);
}
