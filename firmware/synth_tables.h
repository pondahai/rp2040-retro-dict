/* 由 tools/gen_tables.py 產生 —— 不要手改。
 *
 * 要改參數就改 Python 那邊（tools/synth/），再重跑產生器。
 * 手抄幾百個數字到 C 一定會錯，而且錯了只會表現成
 * 「某個字念起來怪怪的」，不會有人去逐格核對。
 */
#ifndef SYNTH_TABLES_H
#define SYNTH_TABLES_H

#include <stdint.h>

#define SYN_SR          16000
#define SYN_BASE_F0_Q8  28160   /* 110.0 Hz */
#define SYN_GLOTTAL_BW  100
#define SYN_SOFT_LIMIT_Q15 18022
#define SYN_TARGET_RMS_Q15 6554
#define SYN_CONSONANT_LEVEL_Q8 115

/* 共振器頻寬。母音窄、噪音寬 —— 窄共振器打在白噪上會變成
 * 有調的鳴響（實測頻譜平坦度 0.0000，嘶聲應該是 0.2）。 */
#define SYN_VOWEL_BW1   90
#define SYN_VOWEL_BW2   130
#define SYN_VOWEL_BW3   200
#define SYN_NOISE_BW1   2000
#define SYN_NOISE_BW2   6000
#define SYN_NOISE_BW3   8000

/* --- 中文 --- */
/* 共振峰目標，(F1,F2,F3) Hz */
static const uint16_t SYN_ZH_VOWEL[13][3] = {
    {  750, 1100, 2700 },   /* A */
    {  550, 1900, 2600 },   /* E */
    {  250,  900, 2300 },   /* N */
    {  360, 1400, 1900 },   /* Z */
    {  800, 1200, 2800 },   /* a */
    {  500, 1300, 2500 },   /* e */
    {  290, 2300, 3000 },   /* i */
    {  250, 1700, 2600 },   /* n */
    {  500,  850, 2700 },   /* o */
    {  490, 1350, 1500 },   /* r */
    {  330,  800, 2300 },   /* u */
    {  300, 1900, 2300 },   /* v */
    {  350, 1600, 2500 },   /* z */
};

#define SYN_ZH_MAX_TARGETS 3
/* 韻母的共振峰目標序列。0xFF 表示序列結束。 */
static const uint8_t SYN_ZH_FINAL[39][3] = {
    { 0x03, 0xFF, 0xFF },   /* __EMPTY_Z */
    { 0x0C, 0xFF, 0xFF },   /* __EMPTY_z */
    { 0x04, 0xFF, 0xFF },   /* a */
    { 0x04, 0x06, 0xFF },   /* ai */
    { 0x04, 0x07, 0xFF },   /* an */
    { 0x00, 0x02, 0xFF },   /* ang */
    { 0x04, 0x0A, 0xFF },   /* ao */
    { 0x05, 0xFF, 0xFF },   /* e */
    { 0x01, 0x06, 0xFF },   /* ei */
    { 0x05, 0x07, 0xFF },   /* en */
    { 0x05, 0x02, 0xFF },   /* eng */
    { 0x09, 0xFF, 0xFF },   /* er */
    { 0x06, 0xFF, 0xFF },   /* i */
    { 0x06, 0x04, 0xFF },   /* ia */
    { 0x06, 0x01, 0x07 },   /* ian */
    { 0x06, 0x00, 0x02 },   /* iang */
    { 0x06, 0x04, 0x0A },   /* iao */
    { 0x06, 0x01, 0xFF },   /* ie */
    { 0x06, 0x07, 0xFF },   /* in */
    { 0x06, 0x02, 0xFF },   /* ing */
    { 0x06, 0x08, 0xFF },   /* io */
    { 0x0B, 0x0A, 0x02 },   /* iong */
    { 0x06, 0x08, 0x0A },   /* iu */
    { 0x08, 0xFF, 0xFF },   /* o */
    { 0x0A, 0x02, 0xFF },   /* ong */
    { 0x08, 0x0A, 0xFF },   /* ou */
    { 0x0A, 0xFF, 0xFF },   /* u */
    { 0x0A, 0x04, 0xFF },   /* ua */
    { 0x0A, 0x04, 0x06 },   /* uai */
    { 0x0A, 0x04, 0x07 },   /* uan */
    { 0x0A, 0x00, 0x02 },   /* uang */
    { 0x0A, 0x05, 0x02 },   /* ueng */
    { 0x0A, 0x05, 0x06 },   /* ui */
    { 0x0A, 0x05, 0x07 },   /* un */
    { 0x0A, 0x08, 0xFF },   /* uo */
    { 0x0B, 0xFF, 0xFF },   /* v */
    { 0x0B, 0x01, 0x07 },   /* van */
    { 0x0B, 0x01, 0xFF },   /* ve */
    { 0x0B, 0x07, 0xFF },   /* vn */
};

typedef enum {
    SYN_K_NONE = 0,
    SYN_K_STOP = 1,
    SYN_K_AFFRICATE = 2,
    SYN_K_FRICATIVE = 3,
    SYN_K_NASAL = 4,
    SYN_K_LATERAL = 5,
    SYN_K_APPROX = 6,
    SYN_K_GLIDE = 7,
} syn_kind;

/* 聲母：(型別, 噪音中心 Hz, 是否送氣) */
static const uint8_t  SYN_ZH_INI_KIND[22] = {
    SYN_K_NONE, SYN_K_STOP, SYN_K_AFFRICATE, SYN_K_AFFRICATE, SYN_K_STOP, SYN_K_FRICATIVE, SYN_K_STOP, SYN_K_FRICATIVE, SYN_K_AFFRICATE, SYN_K_STOP, SYN_K_LATERAL, SYN_K_NASAL, SYN_K_NASAL, SYN_K_STOP, SYN_K_AFFRICATE, SYN_K_APPROX, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_STOP, SYN_K_FRICATIVE, SYN_K_AFFRICATE, SYN_K_AFFRICATE
};
static const uint16_t SYN_ZH_INI_NOISE[22] = { 0, 800, 4500, 2200, 1800, 1200, 1500, 1000, 3000, 1500, 0, 0, 0, 800, 3000, 1400, 5000, 2400, 1800, 3200, 4500, 2200 };
static const uint8_t  SYN_ZH_INI_ASP[22] = { 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0 };

/* 音節 id -> (聲母, 韻母)。順序與 dictbuild/syllable.py 的
 * SYLLABLES 完全一致 —— .DAT 裡存的 id 就是這個順序。 */
#define SYN_ZH_SYLLABLES 428
static const uint8_t SYN_ZH_PARTS[428][2] = {
    {  0,  2 },   /* a */
    {  0,  3 },   /* ai */
    {  0,  4 },   /* an */
    {  0,  5 },   /* ang */
    {  0,  6 },   /* ao */
    {  1,  2 },   /* ba */
    {  1,  3 },   /* bai */
    {  1,  4 },   /* ban */
    {  1,  5 },   /* bang */
    {  1,  6 },   /* bao */
    {  1,  8 },   /* bei */
    {  1,  9 },   /* ben */
    {  1, 10 },   /* beng */
    {  1, 12 },   /* bi */
    {  1, 14 },   /* bian */
    {  1, 16 },   /* biao */
    {  1, 17 },   /* bie */
    {  1, 18 },   /* bin */
    {  1, 19 },   /* bing */
    {  1, 23 },   /* bo */
    {  1, 26 },   /* bu */
    {  2,  2 },   /* ca */
    {  2,  3 },   /* cai */
    {  2,  4 },   /* can */
    {  2,  5 },   /* cang */
    {  2,  6 },   /* cao */
    {  2,  7 },   /* ce */
    {  2,  8 },   /* cei */
    {  2,  9 },   /* cen */
    {  2, 10 },   /* ceng */
    {  3,  2 },   /* cha */
    {  3,  3 },   /* chai */
    {  3,  4 },   /* chan */
    {  3,  5 },   /* chang */
    {  3,  6 },   /* chao */
    {  3,  7 },   /* che */
    {  3,  8 },   /* chei */
    {  3,  9 },   /* chen */
    {  3, 10 },   /* cheng */
    {  3,  0 },   /* chi */
    {  3, 24 },   /* chong */
    {  3, 25 },   /* chou */
    {  3, 26 },   /* chu */
    {  3, 27 },   /* chua */
    {  3, 28 },   /* chuai */
    {  3, 29 },   /* chuan */
    {  3, 30 },   /* chuang */
    {  3, 32 },   /* chui */
    {  3, 33 },   /* chun */
    {  3, 34 },   /* chuo */
    {  2,  1 },   /* ci */
    {  2, 24 },   /* cong */
    {  2, 25 },   /* cou */
    {  2, 26 },   /* cu */
    {  2, 27 },   /* cua */
    {  2, 28 },   /* cuai */
    {  2, 29 },   /* cuan */
    {  2, 30 },   /* cuang */
    {  2, 32 },   /* cui */
    {  2, 33 },   /* cun */
    {  2, 34 },   /* cuo */
    {  4,  2 },   /* da */
    {  4,  3 },   /* dai */
    {  4,  4 },   /* dan */
    {  4,  5 },   /* dang */
    {  4,  6 },   /* dao */
    {  4,  7 },   /* de */
    {  4,  8 },   /* dei */
    {  4,  9 },   /* den */
    {  4, 10 },   /* deng */
    {  4, 12 },   /* di */
    {  4, 13 },   /* dia */
    {  4, 14 },   /* dian */
    {  4, 16 },   /* diao */
    {  4, 17 },   /* die */
    {  4, 19 },   /* ding */
    {  4, 22 },   /* diu */
    {  4, 24 },   /* dong */
    {  4, 25 },   /* dou */
    {  4, 26 },   /* du */
    {  4, 29 },   /* duan */
    {  4, 32 },   /* dui */
    {  4, 33 },   /* dun */
    {  4, 34 },   /* duo */
    {  0,  7 },   /* e */
    {  0,  8 },   /* ei */
    {  0,  9 },   /* en */
    {  0, 10 },   /* eng */
    {  0, 11 },   /* er */
    {  5,  2 },   /* fa */
    {  5,  4 },   /* fan */
    {  5,  5 },   /* fang */
    {  5,  8 },   /* fei */
    {  5,  9 },   /* fen */
    {  5, 10 },   /* feng */
    {  5, 23 },   /* fo */
    {  5, 25 },   /* fou */
    {  5, 26 },   /* fu */
    {  6,  2 },   /* ga */
    {  6,  3 },   /* gai */
    {  6,  4 },   /* gan */
    {  6,  5 },   /* gang */
    {  6,  6 },   /* gao */
    {  6,  7 },   /* ge */
    {  6,  8 },   /* gei */
    {  6,  9 },   /* gen */
    {  6, 10 },   /* geng */
    {  6, 24 },   /* gong */
    {  6, 25 },   /* gou */
    {  6, 26 },   /* gu */
    {  6, 27 },   /* gua */
    {  6, 28 },   /* guai */
    {  6, 29 },   /* guan */
    {  6, 30 },   /* guang */
    {  6, 32 },   /* gui */
    {  6, 33 },   /* gun */
    {  6, 34 },   /* guo */
    {  7,  2 },   /* ha */
    {  7,  3 },   /* hai */
    {  7,  4 },   /* han */
    {  7,  5 },   /* hang */
    {  7,  6 },   /* hao */
    {  7,  7 },   /* he */
    {  7,  8 },   /* hei */
    {  7,  9 },   /* hen */
    {  7, 10 },   /* heng */
    {  7, 24 },   /* hong */
    {  7, 25 },   /* hou */
    {  7, 26 },   /* hu */
    {  7, 27 },   /* hua */
    {  7, 28 },   /* huai */
    {  7, 29 },   /* huan */
    {  7, 30 },   /* huang */
    {  7, 32 },   /* hui */
    {  7, 33 },   /* hun */
    {  7, 34 },   /* huo */
    {  8, 12 },   /* ji */
    {  8, 13 },   /* jia */
    {  8, 14 },   /* jian */
    {  8, 15 },   /* jiang */
    {  8, 16 },   /* jiao */
    {  8, 17 },   /* jie */
    {  8, 18 },   /* jin */
    {  8, 19 },   /* jing */
    {  8, 21 },   /* jiong */
    {  8, 22 },   /* jiu */
    {  8, 35 },   /* ju */
    {  8, 36 },   /* juan */
    {  8, 37 },   /* jue */
    {  8, 38 },   /* jun */
    {  9,  2 },   /* ka */
    {  9,  3 },   /* kai */
    {  9,  4 },   /* kan */
    {  9,  5 },   /* kang */
    {  9,  6 },   /* kao */
    {  9,  7 },   /* ke */
    {  9,  8 },   /* kei */
    {  9,  9 },   /* ken */
    {  9, 10 },   /* keng */
    {  9, 24 },   /* kong */
    {  9, 25 },   /* kou */
    {  9, 26 },   /* ku */
    {  9, 27 },   /* kua */
    {  9, 28 },   /* kuai */
    {  9, 29 },   /* kuan */
    {  9, 30 },   /* kuang */
    {  9, 32 },   /* kui */
    {  9, 33 },   /* kun */
    {  9, 34 },   /* kuo */
    { 10,  2 },   /* la */
    { 10,  3 },   /* lai */
    { 10,  4 },   /* lan */
    { 10,  5 },   /* lang */
    { 10,  6 },   /* lao */
    { 10,  7 },   /* le */
    { 10,  8 },   /* lei */
    { 10, 10 },   /* leng */
    { 10, 12 },   /* li */
    { 10, 13 },   /* lia */
    { 10, 14 },   /* lian */
    { 10, 15 },   /* liang */
    { 10, 16 },   /* liao */
    { 10, 17 },   /* lie */
    { 10, 18 },   /* lin */
    { 10, 19 },   /* ling */
    { 10, 22 },   /* liu */
    { 10, 23 },   /* lo */
    { 10, 24 },   /* long */
    { 10, 25 },   /* lou */
    { 10, 26 },   /* lu */
    { 10, 29 },   /* luan */
    { 10, 33 },   /* lun */
    { 10, 34 },   /* luo */
    { 10, 35 },   /* lv */
    { 10, 37 },   /* lve */
    { 11,  2 },   /* ma */
    { 11,  3 },   /* mai */
    { 11,  4 },   /* man */
    { 11,  5 },   /* mang */
    { 11,  6 },   /* mao */
    { 11,  7 },   /* me */
    { 11,  8 },   /* mei */
    { 11,  9 },   /* men */
    { 11, 10 },   /* meng */
    { 11, 12 },   /* mi */
    { 11, 14 },   /* mian */
    { 11, 16 },   /* miao */
    { 11, 17 },   /* mie */
    { 11, 18 },   /* min */
    { 11, 19 },   /* ming */
    { 11, 22 },   /* miu */
    { 11, 23 },   /* mo */
    { 11, 25 },   /* mou */
    { 11, 26 },   /* mu */
    { 12,  2 },   /* na */
    { 12,  3 },   /* nai */
    { 12,  4 },   /* nan */
    { 12,  5 },   /* nang */
    { 12,  6 },   /* nao */
    { 12,  7 },   /* ne */
    { 12,  8 },   /* nei */
    { 12,  9 },   /* nen */
    { 12, 10 },   /* neng */
    { 12, 12 },   /* ni */
    { 12, 14 },   /* nian */
    { 12, 15 },   /* niang */
    { 12, 16 },   /* niao */
    { 12, 17 },   /* nie */
    { 12, 18 },   /* nin */
    { 12, 19 },   /* ning */
    { 12, 22 },   /* niu */
    { 12, 24 },   /* nong */
    { 12, 25 },   /* nou */
    { 12, 26 },   /* nu */
    { 12, 29 },   /* nuan */
    { 12, 34 },   /* nuo */
    { 12, 35 },   /* nv */
    { 12, 37 },   /* nve */
    {  0, 23 },   /* o */
    {  0, 25 },   /* ou */
    { 13,  2 },   /* pa */
    { 13,  3 },   /* pai */
    { 13,  4 },   /* pan */
    { 13,  5 },   /* pang */
    { 13,  6 },   /* pao */
    { 13,  8 },   /* pei */
    { 13,  9 },   /* pen */
    { 13, 10 },   /* peng */
    { 13, 12 },   /* pi */
    { 13, 14 },   /* pian */
    { 13, 16 },   /* piao */
    { 13, 17 },   /* pie */
    { 13, 18 },   /* pin */
    { 13, 19 },   /* ping */
    { 13, 23 },   /* po */
    { 13, 25 },   /* pou */
    { 13, 26 },   /* pu */
    { 14, 12 },   /* qi */
    { 14, 13 },   /* qia */
    { 14, 14 },   /* qian */
    { 14, 15 },   /* qiang */
    { 14, 16 },   /* qiao */
    { 14, 17 },   /* qie */
    { 14, 18 },   /* qin */
    { 14, 19 },   /* qing */
    { 14, 21 },   /* qiong */
    { 14, 22 },   /* qiu */
    { 14, 35 },   /* qu */
    { 14, 36 },   /* quan */
    { 14, 37 },   /* que */
    { 14, 38 },   /* qun */
    { 15,  2 },   /* ra */
    { 15,  3 },   /* rai */
    { 15,  4 },   /* ran */
    { 15,  5 },   /* rang */
    { 15,  6 },   /* rao */
    { 15,  7 },   /* re */
    { 15,  8 },   /* rei */
    { 15,  9 },   /* ren */
    { 15, 10 },   /* reng */
    { 15,  0 },   /* ri */
    { 15, 24 },   /* rong */
    { 15, 25 },   /* rou */
    { 15, 26 },   /* ru */
    { 15, 27 },   /* rua */
    { 15, 28 },   /* ruai */
    { 15, 29 },   /* ruan */
    { 15, 30 },   /* ruang */
    { 15, 32 },   /* rui */
    { 15, 33 },   /* run */
    { 15, 34 },   /* ruo */
    { 16,  2 },   /* sa */
    { 16,  3 },   /* sai */
    { 16,  4 },   /* san */
    { 16,  5 },   /* sang */
    { 16,  6 },   /* sao */
    { 16,  7 },   /* se */
    { 16,  8 },   /* sei */
    { 16,  9 },   /* sen */
    { 16, 10 },   /* seng */
    { 17,  2 },   /* sha */
    { 17,  3 },   /* shai */
    { 17,  4 },   /* shan */
    { 17,  5 },   /* shang */
    { 17,  6 },   /* shao */
    { 17,  7 },   /* she */
    { 17,  8 },   /* shei */
    { 17,  9 },   /* shen */
    { 17, 10 },   /* sheng */
    { 17,  0 },   /* shi */
    { 17, 24 },   /* shong */
    { 17, 25 },   /* shou */
    { 17, 26 },   /* shu */
    { 17, 27 },   /* shua */
    { 17, 28 },   /* shuai */
    { 17, 29 },   /* shuan */
    { 17, 30 },   /* shuang */
    { 17, 32 },   /* shui */
    { 17, 33 },   /* shun */
    { 17, 34 },   /* shuo */
    { 16,  1 },   /* si */
    { 16, 24 },   /* song */
    { 16, 25 },   /* sou */
    { 16, 26 },   /* su */
    { 16, 27 },   /* sua */
    { 16, 28 },   /* suai */
    { 16, 29 },   /* suan */
    { 16, 30 },   /* suang */
    { 16, 32 },   /* sui */
    { 16, 33 },   /* sun */
    { 16, 34 },   /* suo */
    { 18,  2 },   /* ta */
    { 18,  3 },   /* tai */
    { 18,  4 },   /* tan */
    { 18,  5 },   /* tang */
    { 18,  6 },   /* tao */
    { 18,  7 },   /* te */
    { 18, 10 },   /* teng */
    { 18, 12 },   /* ti */
    { 18, 14 },   /* tian */
    { 18, 16 },   /* tiao */
    { 18, 17 },   /* tie */
    { 18, 19 },   /* ting */
    { 18, 24 },   /* tong */
    { 18, 25 },   /* tou */
    { 18, 26 },   /* tu */
    { 18, 29 },   /* tuan */
    { 18, 32 },   /* tui */
    { 18, 33 },   /* tun */
    { 18, 34 },   /* tuo */
    {  0, 27 },   /* wa */
    {  0, 28 },   /* wai */
    {  0, 29 },   /* wan */
    {  0, 30 },   /* wang */
    {  0, 32 },   /* wei */
    {  0, 33 },   /* wen */
    {  0, 31 },   /* weng */
    {  0, 34 },   /* wo */
    {  0, 26 },   /* wu */
    { 19, 12 },   /* xi */
    { 19, 13 },   /* xia */
    { 19, 14 },   /* xian */
    { 19, 15 },   /* xiang */
    { 19, 16 },   /* xiao */
    { 19, 17 },   /* xie */
    { 19, 18 },   /* xin */
    { 19, 19 },   /* xing */
    { 19, 21 },   /* xiong */
    { 19, 22 },   /* xiu */
    { 19, 35 },   /* xu */
    { 19, 36 },   /* xuan */
    { 19, 37 },   /* xue */
    { 19, 38 },   /* xun */
    {  0, 13 },   /* ya */
    {  0, 14 },   /* yan */
    {  0, 15 },   /* yang */
    {  0, 16 },   /* yao */
    {  0, 17 },   /* ye */
    {  0, 12 },   /* yi */
    {  0, 18 },   /* yin */
    {  0, 19 },   /* ying */
    {  0, 20 },   /* yo */
    {  0, 21 },   /* yong */
    {  0, 22 },   /* you */
    {  0, 35 },   /* yu */
    {  0, 36 },   /* yuan */
    {  0, 37 },   /* yue */
    {  0, 38 },   /* yun */
    { 20,  2 },   /* za */
    { 20,  3 },   /* zai */
    { 20,  4 },   /* zan */
    { 20,  5 },   /* zang */
    { 20,  6 },   /* zao */
    { 20,  7 },   /* ze */
    { 20,  8 },   /* zei */
    { 20,  9 },   /* zen */
    { 20, 10 },   /* zeng */
    { 21,  2 },   /* zha */
    { 21,  3 },   /* zhai */
    { 21,  4 },   /* zhan */
    { 21,  5 },   /* zhang */
    { 21,  6 },   /* zhao */
    { 21,  7 },   /* zhe */
    { 21,  8 },   /* zhei */
    { 21,  9 },   /* zhen */
    { 21, 10 },   /* zheng */
    { 21,  0 },   /* zhi */
    { 21, 24 },   /* zhong */
    { 21, 25 },   /* zhou */
    { 21, 26 },   /* zhu */
    { 21, 27 },   /* zhua */
    { 21, 28 },   /* zhuai */
    { 21, 29 },   /* zhuan */
    { 21, 30 },   /* zhuang */
    { 21, 32 },   /* zhui */
    { 21, 33 },   /* zhun */
    { 21, 34 },   /* zhuo */
    { 20,  1 },   /* zi */
    { 20, 24 },   /* zong */
    { 20, 25 },   /* zou */
    { 20, 26 },   /* zu */
    { 20, 27 },   /* zua */
    { 20, 28 },   /* zuai */
    { 20, 29 },   /* zuan */
    { 20, 30 },   /* zuang */
    { 20, 32 },   /* zui */
    { 20, 33 },   /* zun */
    { 20, 34 },   /* zuo */
};

/* 空韻（zi/ci/si 與 zhi/chi/shi/ri 的 i 不是真的 i）已經
 * 併進上面的韻母表，SYN_ZH_PARTS 直接指過去。漏掉這個會讓
 * 整批極常用音節消失 —— 實測 bu4 shi4 只念得出「不」。 */

/* 聲調曲線。每個點是 (位置 Q8, 基頻倍率 Q8)。 */
#define SYN_TONE_MAX_PTS 4
static const uint8_t SYN_TONE_NPTS[5] = { 2, 3, 3, 4, 3 };
static const uint16_t SYN_TONE_CURVE[5][4][2] = {
    { {     0,   271 }, {   256,   271 }, { 0, 0 }, { 0, 0 } },   /* 聲調 0 */
    { {     0,   358 }, {   128,   358 }, {   256,   358 }, { 0, 0 } },   /* 聲調 1 */
    { {     0,   271 }, {   128,   312 }, {   256,   358 }, { 0, 0 } },   /* 聲調 2 */
    { {     0,   236 }, {    90,   205 }, {   192,   205 }, {   256,   271 } },   /* 聲調 3 */
    { {     0,   358 }, {   128,   271 }, {   256,   205 }, { 0, 0 } },   /* 聲調 4 */
};
static const uint16_t SYN_TONE_DUR[5] = { 120, 230, 240, 300, 200 };
static const uint16_t SYN_NEUTRAL_AFTER_Q8[5] = { 154, 154, 159, 192, 115 };
#define SYN_FINAL_LENGTHEN_PCT 115
#define SYN_GAP_MS 60
#define SYN_MAX_SEG_SAMPLES 5584

/* --- 英文 --- */
#define SYN_EN_PHONEMES 43
/* 音素 id 的順序與 synth/phoneme.py 的 PHONEMES 一致 ——
 * .DAT 的 SYL_EN 存的就是這個順序的索引。 */
static const uint16_t SYN_EN_FORMANT[43][3] = {
    {    0,    0,    0 },   /* p */
    {    0,    0,    0 },   /* b */
    {    0,    0,    0 },   /* t */
    {    0,    0,    0 },   /* d */
    {    0,    0,    0 },   /* k */
    {    0,    0,    0 },   /* g */
    {    0,    0,    0 },   /* f */
    {    0,    0,    0 },   /* v */
    {    0,    0,    0 },   /* th */
    {    0,    0,    0 },   /* dh */
    {    0,    0,    0 },   /* s */
    {    0,    0,    0 },   /* z */
    {    0,    0,    0 },   /* sh */
    {    0,    0,    0 },   /* zh */
    {    0,    0,    0 },   /* h */
    {    0,    0,    0 },   /* ch */
    {    0,    0,    0 },   /* jh */
    {  250, 1100, 2300 },   /* m */
    {  250, 1700, 2600 },   /* n */
    {  250,  900, 2300 },   /* ng */
    {  350, 1100, 2600 },   /* l */
    {  400, 1300, 1500 },   /* r */
    {  300, 2200, 3000 },   /* y */
    {  330,  800, 2200 },   /* w */
    {  300, 2300, 3000 },   /* iy */
    {  400, 1900, 2550 },   /* ih */
    {  550, 1800, 2500 },   /* eh */
    {  700, 1700, 2400 },   /* ae */
    {  750, 1100, 2500 },   /* aa */
    {  680, 1450, 2500 },   /* ah */
    {  570,  850, 2500 },   /* ao */
    {  450, 1100, 2350 },   /* uh */
    {  320,  900, 2200 },   /* uw */
    {  490, 1350, 1500 },   /* er */
    {  500, 1400, 2500 },   /* ax */
    {    0,    0,    0 },   /* ey */
    {    0,    0,    0 },   /* ay */
    {    0,    0,    0 },   /* oy */
    {    0,    0,    0 },   /* ow */
    {    0,    0,    0 },   /* aw */
    {    0,    0,    0 },   /* ia */
    {    0,    0,    0 },   /* ea */
    {    0,    0,    0 },   /* ua */
};

static const uint8_t SYN_EN_KIND[43] = {
    SYN_K_STOP, SYN_K_STOP, SYN_K_STOP, SYN_K_STOP, SYN_K_STOP, SYN_K_STOP, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_FRICATIVE, SYN_K_AFFRICATE, SYN_K_AFFRICATE, SYN_K_NASAL, SYN_K_NASAL, SYN_K_NASAL, SYN_K_LATERAL, SYN_K_APPROX, SYN_K_GLIDE, SYN_K_GLIDE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE, SYN_K_NONE
};
static const uint16_t SYN_EN_NOISE[43] = { 800, 800, 1800, 1800, 1500, 1500, 1400, 1400, 1600, 1600, 5000, 5000, 2400, 2400, 1000, 2400, 2400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t SYN_EN_ASP[43] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* 是不是母音／雙母音；雙母音的兩個目標 */
static const uint8_t SYN_EN_IS_VOWEL[43] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t SYN_EN_DIPH[43][2] = {
    { 0xFF, 0xFF },   /* p */
    { 0xFF, 0xFF },   /* b */
    { 0xFF, 0xFF },   /* t */
    { 0xFF, 0xFF },   /* d */
    { 0xFF, 0xFF },   /* k */
    { 0xFF, 0xFF },   /* g */
    { 0xFF, 0xFF },   /* f */
    { 0xFF, 0xFF },   /* v */
    { 0xFF, 0xFF },   /* th */
    { 0xFF, 0xFF },   /* dh */
    { 0xFF, 0xFF },   /* s */
    { 0xFF, 0xFF },   /* z */
    { 0xFF, 0xFF },   /* sh */
    { 0xFF, 0xFF },   /* zh */
    { 0xFF, 0xFF },   /* h */
    { 0xFF, 0xFF },   /* ch */
    { 0xFF, 0xFF },   /* jh */
    { 0xFF, 0xFF },   /* m */
    { 0xFF, 0xFF },   /* n */
    { 0xFF, 0xFF },   /* ng */
    { 0xFF, 0xFF },   /* l */
    { 0xFF, 0xFF },   /* r */
    { 0xFF, 0xFF },   /* y */
    { 0xFF, 0xFF },   /* w */
    { 0xFF, 0xFF },   /* iy */
    { 0xFF, 0xFF },   /* ih */
    { 0xFF, 0xFF },   /* eh */
    { 0xFF, 0xFF },   /* ae */
    { 0xFF, 0xFF },   /* aa */
    { 0xFF, 0xFF },   /* ah */
    { 0xFF, 0xFF },   /* ao */
    { 0xFF, 0xFF },   /* uh */
    { 0xFF, 0xFF },   /* uw */
    { 0xFF, 0xFF },   /* er */
    { 0xFF, 0xFF },   /* ax */
    { 26, 24 },   /* ey */
    { 28, 24 },   /* ay */
    { 30, 24 },   /* oy */
    { 30, 32 },   /* ow */
    { 28, 32 },   /* aw */
    { 24, 34 },   /* ia */
    { 26, 34 },   /* ea */
    { 32, 34 },   /* ua */
};
static const uint8_t SYN_EN_IS_LONG[43] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define SYN_EN_DUR_VOWEL      120
#define SYN_EN_DUR_DIPHTHONG  170
#define SYN_EN_DUR_CLOSURE    45
#define SYN_EN_DUR_BURST      8
#define SYN_EN_DUR_ASPIRATION 40
#define SYN_EN_DUR_FRICATIVE  90
#define SYN_EN_DUR_NASAL      65
#define SYN_EN_DUR_GLIDE      55
#define SYN_EN_LONG_FACTOR_Q8 384
#define SYN_EN_DECL_Q8 56
static const uint16_t SYN_EN_STRESS_DUR_Q8[3] = { 205, 346, 269 };
static const uint16_t SYN_EN_STRESS_F0_Q8[3] = { 236, 302, 264 };

#endif /* SYNTH_TABLES_H */
