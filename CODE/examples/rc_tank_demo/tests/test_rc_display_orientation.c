#include "rc_display_orientation.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void)
{
    const rc_display_orientation_t tank =
        rc_display_orientation_for_role(RC_DISPLAY_ROLE_TANK);
    TEST_ASSERT(tank.swap_xy);
    TEST_ASSERT(!tank.mirror_x);
    TEST_ASSERT(tank.mirror_y);

    const rc_display_orientation_t remote =
        rc_display_orientation_for_role(RC_DISPLAY_ROLE_REMOTE);
    TEST_ASSERT(remote.swap_xy);
    TEST_ASSERT(!remote.mirror_x);
    TEST_ASSERT(remote.mirror_y);
    return 0;
}
