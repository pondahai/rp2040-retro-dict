/* 把 C 版 letter-to-sound 的結果印出來，給 compare_lts.py 跟 Python 規格比對。
 *
 *   test_lts <字串> [<字串> ...]
 *
 * 每行輸出：<原字串> TAB <音素 id 以空白分隔>
 */

#include <stdio.h>
#include <string.h>

#include "lts.h"

#define MAXIDS 256

int main(int argc, char **argv)
{
    int a;

    if (argc < 2) {
        fprintf(stderr, "用法：test_lts <字串> [...]\n");
        return 1;
    }
    for (a = 1; a < argc; a++) {
        uint16_t ids[MAXIDS];
        int n = lts_to_ids(argv[a], ids, MAXIDS);
        int i;
        printf("%s\t", argv[a]);
        for (i = 0; i < n; i++)
            printf("%s%u", i ? " " : "", (unsigned)ids[i]);
        printf("\n");
    }
    return 0;
}
