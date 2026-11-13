#include <platform.h>
void run_plmpeg_benchmark(void);
int main(void)
{
    par {
        on tile[1]: run_plmpeg_benchmark();
    }
    return 0;
}
