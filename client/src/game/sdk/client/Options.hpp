// ModCore — game SDK: Options (options.txt readers)
#pragma once
namespace mc {
namespace sdk {
// horizontal FOV from options.txt (field_of_view_new / field_of_view);
// cached, clamped 30..110, default 70
float options_fov();
}
}
