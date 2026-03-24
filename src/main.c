#include "version.h"
#include <stdio.h>

int main() {
    printf("Version: %s (%s)%s, built: %s\n", PROJECT_GIT_DESCRIBE,
           PROJECT_GIT_COMMIT, PROJECT_GIT_DIRTY ? "-dirty" : "",
           PROJECT_BUILD_TIMESTAMP);
    return 0;
}
