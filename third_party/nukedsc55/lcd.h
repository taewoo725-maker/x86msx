/*
 * Copyright (C) 2021, 2024 nukeykt
 *
 * This file is part of Nuked-SC55.
 * X86MSX: 화면 창(SDL) 을 뺀 판 — LCD 제어기 자료만 남김 (원본 src/lcd.h, GPL-2.0-or-later)
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int lcd_width;
extern int lcd_height;
extern uint32_t lcd_col1;
extern uint32_t lcd_col2;
extern uint8_t LCD_Data[80];
extern uint8_t LCD_CG[64];
extern uint8_t lcd_enable;

void LCD_Write(uint32_t address, uint8_t data);
void LCD_Enable(uint32_t enable);
