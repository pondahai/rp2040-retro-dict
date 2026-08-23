/* 在 PC 上驗注音引擎。
 *
 *   test_ime <按鍵串> [<按鍵串> ...]
 *
 * 每行輸出：<按鍵串> TAB <注音符號> TAB <候選字串>
 * compare_ime.py 拿它跟直接讀同一份碼表的 Python 實作比對。
 */

#include <stdio.h>
#include "ime.h"

int main(int argc, char **argv)
{
    int a;
    if (argc < 2) {
        fprintf(stderr, "用法：test_ime <按鍵串> [...]\n");
        return 1;
    }
    for (a = 1; a < argc; a++) {
        /* 查不到時 ime_query 不動 out —— 不清空的話會印出上一輪的
           殘留，看起來像「查得到」。這種假象正是 HANDOVER 教訓一。 */
        char bopo[IME_MAX_BOPO] = { 0 }, cands[512] = { 0 };
        ime_bopomofo(argv[a], bopo, sizeof(bopo));
        ime_query(argv[a], cands, sizeof(cands));
        printf("%s\t%s\t%s\n", argv[a], bopo, cands);
    }
    return 0;
}
