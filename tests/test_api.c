#include "bink2.h"
#include "test_common.h"

int main(void)
{
    unsigned long frames = 0;
    bink2 b;

    CHECK(bink2_open_file("/nonexistent.bk2", BINK2_MODE_DISK) == NULL);
    bink2_close(NULL);
    CHECK(bink2_info_all(NULL, NULL, &frames, NULL) != 0);
    CHECK(bink2_get_plane(NULL, BINK2_PLANE_Y) == NULL);
    CHECK(bink2_enable_filters(NULL, 0) == BINK2_ERROR);
    b = bink2_open_file(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2",
                        BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_enable_filters(b, 0) == 0);
    CHECK(bink2_enable_filters(b, 1) == 0);
    bink2_close(b);
    return 0;
}
