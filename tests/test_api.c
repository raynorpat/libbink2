#include "bink2.h"
#include "test_common.h"

int main(void)
{
    unsigned long frames = 0;

    CHECK(bink2_open_file("/nonexistent.bk2", BINK2_MODE_DISK) == NULL);
    bink2_close(NULL);
    CHECK(bink2_info_all(NULL, NULL, &frames, NULL) != 0);
    CHECK(bink2_get_plane(NULL, BINK2_PLANE_Y) == NULL);
    return 0;
}
