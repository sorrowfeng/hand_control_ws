# 一只手的完整触觉快照，一手一条，按 tactile_publish_rate_hz 发布。
# sites 覆盖全部 LSS_* 传感位点（5 指尖 + 5 指腹 + 手掌）。

std_msgs/Header header
string hand_name
string bus_name
int32 address
TactileSite[] sites
