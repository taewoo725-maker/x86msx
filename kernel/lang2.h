/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * lang2.h - 추가 화면 언어 (독일어, 프랑스어, 스페인어, 스웨덴어, 러시아어, 브라질 포르투갈어, 에스토니아어)
 * 메뉴와 자주 보는 문구만 번역하고, 없는 문구는 영어(lang_en)로 대신한다.
 * 아랍어는 오른쪽→왼쪽 쓰기와 글자 이어 쓰기(셰이핑)가 필요해 별도 단계로 한다.
 * tools/mkfont.py가 이 파일의 글자도 글꼴에 넣는다.
 */
#ifndef MSX_LANG2_H
#define MSX_LANG2_H
#include "lang.h"

typedef struct { StrId id; const char *text; } LangPair;

#define L2(id, de, fr, es, sv, ru, pt, et) { id, { de, fr, es, sv, ru, pt, et } }
typedef struct { StrId id; const char *t[7]; } Lang2Row;

static const Lang2Row lang2_rows[] = {
    L2(S_SP_IDE, "IDE-Festplatte (Sunrise)", "Disque dur IDE (Sunrise)", "Disco duro IDE (Sunrise)", "IDE-hårddisk (Sunrise)", "Жёсткий диск IDE (Sunrise)", "Disco rígido IDE (Sunrise)", "IDE kõvaketas (Sunrise)"),
    L2(S_M_FILE, "Datei", "Fichier", "Archivo", "Arkiv", "Файл", "Arquivo", "Fail"),
    L2(S_M_RUN, "Ausführen", "Exécution", "Ejecutar", "Kör", "Запуск", "Executar", "Käivita"),
    L2(S_M_OPTIONS, "Optionen", "Options", "Opciones", "Alternativ", "Параметры", "Opções", "Valikud"),
    L2(S_M_TOOLS, "Werkzeuge", "Outils", "Herramientas", "Verktyg", "Сервис", "Ferramentas", "Tööriistad"),
    L2(S_M_HELP, "Hilfe", "Aide", "Ayuda", "Hjälp", "Справка", "Ajuda", "Abi"),
    L2(S_CART1, "Cartridge-Slot 1", "Port cartouche 1", "Ranura de cartucho 1", "Kassettplats 1", "Слот картриджа 1", "Slot de cartucho 1", "Kasseti pesa 1"),
    L2(S_CART2, "Cartridge-Slot 2", "Port cartouche 2", "Ranura de cartucho 2", "Kassettplats 2", "Слот картриджа 2", "Slot de cartucho 2", "Kasseti pesa 2"),
    L2(S_DISK_A, "Diskettenlaufwerk A", "Lecteur de disquette A", "Unidad de disco A", "Diskettstation A", "Дисковод A", "Unidade de disquete A", "Disketiseade A"),
    L2(S_DISK_B, "Diskettenlaufwerk B", "Lecteur de disquette B", "Unidad de disco B", "Diskettstation B", "Дисковод B", "Unidade de disquete B", "Disketiseade B"),
    L2(S_INSERT, "Einlegen", "Insérer", "Insertar", "Sätt i", "Вставить", "Inserir", "Sisesta"),
    L2(S_EJECT, "Auswerfen", "Éjecter", "Expulsar", "Mata ut", "Извлечь", "Ejetar", "Väljasta"),
    L2(S_INSERT_SPECIAL, "Spezial-Cartridge einlegen", "Insérer une cartouche spéciale", "Insertar cartucho especial", "Sätt i specialkassett", "Вставить спецкартридж", "Inserir cartucho especial", "Sisesta erikassett"),
    L2(S_RESET_AFTER_INSERT, "Reset nach dem Einlegen", "Reset après insertion", "Reiniciar al insertar", "Återställ efter isättning", "Сброс после вставки", "Reiniciar após inserir", "Lähtesta pärast sisestamist"),
    L2(S_REBOOT_PC, "PC neu starten", "Redémarrer le PC", "Reiniciar el PC", "Starta om datorn", "Перезагрузить ПК", "Reiniciar o PC", "Taaskäivita arvuti"),
    L2(S_EXIT, "Beenden (ausschalten)", "Quitter (éteindre)", "Salir (apagar)", "Avsluta (stäng av)", "Выход (выключить)", "Sair (desligar)", "Välju (lülita välja)"),
    L2(S_RUN_PAUSE, "Start / Pause", "Exécuter / Pause", "Ejecutar / Pausa", "Kör / Paus", "Пуск / Пауза", "Executar / Pausar", "Käivita / Paus"),
    L2(S_HARD_RESET, "Hard-Reset", "Reset matériel", "Reinicio completo", "Hård återställning", "Аппаратный сброс", "Reset completo", "Riistvaraline lähtestus"),
    L2(S_SOFT_RESET, "Soft-Reset", "Reset logiciel", "Reinicio suave", "Mjuk återställning", "Программный сброс", "Reset suave", "Tarkvaraline lähtestus"),
    L2(S_FULL_RESET, "Vollständiger Reset", "Reset complet", "Reinicio total", "Fullständig återställning", "Полный сброс", "Reset total", "Täielik lähtestus"),
    L2(S_EMULATION, "Emulation", "Émulation", "Emulación", "Emulering", "Эмуляция", "Emulação", "Emuleerimine"),
    L2(S_VIDEO, "Video", "Vidéo", "Vídeo", "Video", "Видео", "Vídeo", "Video"),
    L2(S_EFFECTS, "Effekte", "Effets", "Efectos", "Effekter", "Эффекты", "Efeitos", "Efektid"),
    L2(S_SOUND, "Sound", "Son", "Sonido", "Ljud", "Звук", "Som", "Heli"),
    L2(S_LANGUAGE, "Sprache", "Langue", "Idioma", "Språk", "Язык", "Idioma", "Keel"),
    L2(S_MIXER_MENU, "Mischpult...", "Mixeur...", "Mezclador...", "Mixer...", "Микшер...", "Mixer...", "Mikser..."),
    L2(S_KB_MENU, "Tastatur...", "Clavier...", "Teclado...", "Tangentbord...", "Клавиатура...", "Teclado...", "Klaviatuur..."),
    L2(S_JOY_MENU, "Joystick...", "Joystick...", "Joystick...", "Styrspak...", "Джойстик...", "Joystick...", "Juhtkang..."),
    L2(S_MOUSE_MENU, "Maus...", "Souris...", "Ratón...", "Mus...", "Мышь...", "Mouse...", "Hiir..."),
    L2(S_USB_GROW, "Ganze USB-Kapazität nutzen", "Utiliser toute la clé USB", "Usar toda la capacidad USB", "Använd hela USB-minnet", "Использовать весь USB", "Usar toda a capacidade do USB", "Kasuta kogu USB mahtu"),
    L2(S_DIAG, "Diagnose", "Diagnostic", "Diagnóstico", "Diagnostik", "Диагностика", "Diagnóstico", "Diagnostika"),
    L2(S_ABOUT, "Über X86MSX", "À propos de X86MSX", "Acerca de X86MSX", "Om X86MSX", "О программе X86MSX", "Sobre o X86MSX", "X86MSX teave"),
    L2(S_PAUSED, "Pausiert", "En pause", "En pausa", "Pausad", "Пауза", "Pausado", "Peatatud"),
    L2(S_STOPPED, "Gestoppt", "Arrêté", "Detenido", "Stoppad", "Остановлено", "Parado", "Seisab"),
    L2(S_VOLUME, "Lautstärke", "Volume", "Volumen", "Volym", "Громкость", "Volume", "Helitugevus"),
    L2(S_MUTED, "Stumm", "Muet", "Silencio", "Tyst", "Без звука", "Mudo", "Vaigistatud"),
    L2(S_SPEED, "Geschwindigkeit", "Vitesse", "Velocidad", "Hastighet", "Скорость", "Velocidade", "Kiirus"),
    L2(S_DRIVES, "Laufwerke", "Lecteurs", "Unidades", "Enheter", "Диски", "Unidades", "Kettad"),
    L2(S_COL_NAME, "Dateiname", "Nom", "Nombre", "Filnamn", "Имя файла", "Nome", "Failinimi"),
    L2(S_COL_TYPE, "Typ", "Type", "Tipo", "Typ", "Тип", "Tipo", "Tüüp"),
    L2(S_COL_MAPPER, "Mapper", "Mapper", "Mapper", "Mapper", "Маппер", "Mapper", "Mapper"),
    L2(S_COL_SIZE, "Größe", "Taille", "Tamaño", "Storlek", "Размер", "Tamanho", "Suurus"),
    L2(S_COL_DATE, "Datum", "Date", "Fecha", "Datum", "Дата", "Data", "Kuupäev"),
    L2(S_PROPS, "Eigenschaften", "Propriétés", "Propiedades", "Egenskaper", "Свойства", "Propriedades", "Atribuudid"),
    L2(S_TAB_EMU, "Emulation", "Émulation", "Emulación", "Emulering", "Эмуляция", "Emulação", "Emuleerimine"),
    L2(S_TAB_MIXER, "Mischpult", "Mixeur", "Mezclador", "Mixer", "Микшер", "Mixer", "Mikser"),
    L2(S_TAB_KEYBOARD, "Tastatur", "Clavier", "Teclado", "Tangentbord", "Клавиатура", "Teclado", "Klaviatuur"),
    L2(S_TAB_JOY, "Joystick", "Joystick", "Joystick", "Styrspak", "Джойстик", "Joystick", "Juhtkang"),
    L2(S_TAB_MOUSE, "Maus", "Souris", "Ratón", "Mus", "Мышь", "Mouse", "Hiir"),
    L2(S_WRITE_PROTECT, "Schreibschutz", "Protection en écriture", "Protección contra escritura", "Skrivskydd", "Защита от записи", "Proteção contra gravação", "Kirjutuskaitse"),
    L2(S_MONITOR, "Monitortyp", "Type de moniteur", "Tipo de monitor", "Bildskärmstyp", "Тип монитора", "Tipo de monitor", "Monitori tüüp"),
    L2(S_SCANLINES, "Scanlines", "Lignes de balayage", "Líneas de escaneo", "Sveplinjer", "Строки развёртки", "Linhas de varredura", "Skaneerimisjooned"),
    L2(S_OUTPUT, "Ausgabegerät", "Périphérique de sortie", "Dispositivo de salida", "Utmatningsenhet", "Устройство вывода", "Dispositivo de saída", "Väljundseade"),
    L2(S_MSX_MACHINE, "MSX-Modell", "Machine MSX", "Máquina MSX", "MSX-maskin", "Модель MSX", "Máquina MSX", "MSX-masin"),
    L2(S_NONE, "keine", "aucune", "ninguno", "ingen", "нет", "nenhum", "puudub"),
};
#undef L2

/* 언어 메뉴에 보일 이름 (각 나라 말) */
static const char *const lang2_names[7] = { "Deutsch", "Français", "Español", "Svenska", "Русский", "Português (Brasil)", "Eesti" };
static const char *const lang2_codes[7] = { "de", "fr", "es", "sv", "ru", "pt", "et" };

#endif
