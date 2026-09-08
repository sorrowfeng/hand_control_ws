# 单个触觉传感位点的读数。
#
# 五个数组的长度都等于 cell_count。某个通道读取失败时该数组为空，
# 因此可以用 empty() 区分「读取失败」和「读到全零」。
#
# 量纲：
#   pressure          归一化 [0.0, 1.0]，相对 set_finger_pressure_reset 设定的零点
#   normal_force      法向力，单位 N
#   tangential_force  切向力，单位 N
#   force_direction   受力方向，单位 deg，[0, 360]
#   proximity         接近程度，归一化 [0.0, 1.0]

int32 sensor_id
int32 cell_count

float64[] pressure
float64[] normal_force
float64[] tangential_force
float64[] force_direction
float64[] proximity
