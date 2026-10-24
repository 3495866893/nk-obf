#include <定义.h>
#include <random>
std::mt19937 generator(static_cast<unsigned int>(time(0))); // 使用当前时间作为种子
std::uniform_int_distribution<int> distribution_offset(-150, 150); // 生成-80到80之间的随机偏移量
 int findminat()
{
 float min = NumIo[3];
 float DistanceMin = 400.0;
 int minAt = 999;
 for (int i = 0; i < MaxPlayerCount; i++)
 {
if (IsAimLongAim)
{
 if (strcmp(Aim[i].Name, AimName) == 0)
 {
  minAt = i;
 }
}
else
{
 if (Aim[i].ScreenDistance < min)
 {
  if (DrawIo[25])
  {
//持续锁定
strcpy(AimName, Aim[i].Name);
  }
  min = Aim[i].ScreenDistance;
  minAt = i;
 }
}
  }
 if (minAt == 999)
 {
  Gmin = -1;
  IsAimLongAim = false;
  return -1;
 }
 Gmin = minAt;
 Aim[minAt].WodDistance;
 if (DrawIo[25])
 {
  IsAimLongAim = true;
 }
 return minAt;
}


 float GetWeaponId(int WeaponId)
{
 switch (WeaponId)
 {
  //突击步枪
  case 101008:
  return 1.2;// M762
  break;
  case 101001:
  return 1.1;// AKM
  break;
  case 101004:
  return 0.8;// M416
  break;
  case 101003:
  return 0.8;// SCAR-L
  break;
  case 101002:
  return 0.85;// M16A4
  break;
  case 101009:
  return 1.05;// Mk47
  break;
  case 101006:
  return 0.75;// AUG
  break;
  case 101005:
  return 1.05;// Groza
  break;
  case 101010:
  return 1.15;// G36C
  break;
  case 101007:
  return 0.95;// QBZ
  break;
  case 101011:
  return 1.0;// AC-VAL
  break;
  case 101012:
  return 0.90;// 蜜獾突击步枪
  break;
  //机关枪
  case 105001:
  return 1.3;// M249
  break;
  case 105002:
  return 1.3;// DP-28
  case 105010:
  return 1.05;// MG3
  break;
  //冲锋枪
  case 102001:
  return 0.6;// UZI
  break;
  case 102003:
  return 0.6;// Vector
  break;
  case 100103:
  return 0.65;// PP-19
  break;
  case 102007:
  return 0.6;// MP5K
  break;
  case 102002:
  return 0.6;// UMP 5
  break;
  case 102004:
  return 0.6;// 汤姆逊
  break;
  case 102105:
  return 0.5;// P90
  break;
  case 102005:
  return 0.6;// 野牛
  break;
  default:
  return 0.95;// 未收录
  break;
 }
 return 1.0;// 未获取
}

 float getScopeAcc(int Scope)
{
 switch (Scope)
 {
  case 0:
  return 1.0f;
  break;
  case 1:
  return .98f;
  break;
  case 2:
  return .95f;
  break;
  case 3:
  return .94f;
  break;
  case 4:
  return .92f;
  break;
  case 5:
  return .9f;
  break;
  case 6:
  return .88f;
  break;
  case 7:
  return .86f;
  break;
  case 8:
  return .85f;
  break;
  default:
  return .9f;
  break;
 }
}
 float 散弹枪(int WeaponId)
{
 switch (WeaponId)
 {
 case 104005:
  return 1.0;
  break;
 case 104001:
  return 1.0;
  break;
 case 104002:
  return 1.0;
  break;
 case 104003:
  return 1.0;
  break;
 case 104004:
  return 1.0;
  break;
 case 104100:
  return 1.0;
  break;
 case 104101:
  return 1.0;
  break;
 case 104102:
  return 1.0;
  break;
 //适配地铁武器
 case 1040011:
  return 1.0;
  break;
 case 1040012:
  return 1.0;
  break;
 case 1040013:
  return 1.0;
  break;
 case 1040014:
  return 1.0;
  break;
 case 1040015:
  return 1.0;
  break;
 case 1040022:
  return 1.0;
  break;
 case 1040023:
  return 1.0;
  break;
 case 1040024:
  return 1.0;
  break;
 case 1040025:
  return 1.0;
  break;
 case 1040031:
  return 1.0;
  break;
 case 1040032:
  return 1.0;
  break;
 case 1040033:
  return 1.0;
  break;
 case 1040034:
  return 1.0;
  break;
 case 1040035:
  return 1.0;
  break;
 case 1040041:
  return 1.0;
  break;
 case 1040042:
  return 1.0;
  break;
 case 1040043:
  return 1.0;
  break;
 case 1040044:
  return 1.0;
  break;
 case 1040045:
  return 1.0;
  break;
 case 1041022:
  return 1.0;
  break;
 case 1041024:
  return 1.0;
  break;
 case 1041025:
  return 1.0;
  break;
 }
 return 0.0; // 不是喷子
}
 void AimBotAuto(ImDrawList *Draw)
// 自瞄线程
{
 bool isDown = false;
 // 是否按下触摸
 
 double leenx = 0.0f;
 // x轴速度
 
 double leeny = 0.0f;
 // y轴速度
 
 double tx = NumIo[5] + distribution_offset(generator);
 double ty = NumIo[6] + distribution_offset(generator);

 // 触摸点位置
 
 float SpeedMin = 2.0f;
 // 临时触摸速度
 
 double w = 0.0f, h = 0.0f, cmp = 0.0f;
 // 宽度 高度 正切
 double ScreenX = displayInfo.width > displayInfo.height ? displayInfo.height : displayInfo.width;
 double ScreenY = displayInfo.width > displayInfo.height ? displayInfo.width : displayInfo.height;
 // 分辨率(竖屏)PS:滑屏用的坐标是竖屏状态下的

 double ScrXH = ScreenX / 2.0f;
 // 一半屏幕X
 
 double ScrYH = ScreenY / 2.0f;
 // 一半屏幕X
 
 static float TargetX = 0;
 static float TargetY = 0;
 // 触摸目标位置
 Vector3A obj;
 float PushX;
 /* 压枪X */
 float PushY;
 /* 压枪Y */
 
 float NowCoor[3];
 //Touch_Init();
 
 while (1)
 {
ImGuiIO& iooi = ImGui::GetIO();
		
		if (DrawIo[21] && iooi.MouseDown[0] && iooi.MousePos.x <= NumIo[6] + NumIo[7] && iooi.MousePos.y <= py*2 - NumIo[5] + NumIo[7] && iooi.MousePos.x >= NumIo[6] - NumIo[7] && iooi.MousePos.y >= py*2 - NumIo[5] - NumIo[7])
        {           
        	usleep(30000);      
            if (DrawIo[21] && iooi.MouseDown[0] && iooi.MousePos.x <= NumIo[6] + NumIo[7] && iooi.MousePos.y <= py*2 - NumIo[5] + NumIo[7] && iooi.MousePos.x >= NumIo[6] - NumIo[7] && iooi.MousePos.y >= py*2 - NumIo[5] - NumIo[7])
        	{
           		while (DrawIo[21] && iooi.MouseDown[0] && iooi.MousePos.x <= NumIo[6] + NumIo[7] && iooi.MousePos.y <= py*2 - NumIo[5] + NumIo[7] && iooi.MousePos.x >= NumIo[6] - NumIo[7] && iooi.MousePos.y >= py*2 - NumIo[5] - NumIo[7])
                {			
    				NumIo[6] = iooi.MousePos.x;
                    NumIo[5] = py*2 - iooi.MousePos.y;
					TouchingColor = ImColor(0, 220, 0, 150);
    				usleep(500);           				
                }		
				TouchingColor = ImColor(255, 0, 0, 150);
            }
        }
  if (!DrawIo[20])
  {
IsAimLongAim = false;
if (isDown == true)
{
 usleep(1000);
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 // 恢复变量
 Touch_Up();
 // 抬起
 isDown = false;
}
usleep(NumIo[16] * 1000);
continue;
  }
  
  findminat();
  // 获取目标
  if (Gmin == -1)
  {
IsAimLongAim = false;
if (isDown == true)
{
 usleep(1000);
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 // 恢复变量
 Touch_Up();
 // 抬起
 isDown = false;
}
usleep(NumIo[16] * 1000);
continue;
  }
  float ToReticleDistance = Aim[Gmin].ScreenDistance;
  float FlyTime = Aim[Gmin].WodDistance / NumIo[11];
  float DropM = 540.0f * FlyTime * FlyTime;
  int 开火判断;
  if (Aim[Gmin].WodDistance > NumIo[39]){
  开火判断 =  Firing;
  }else{
  开火判断 = 0;
  }
NowCoor[0] = Aim[Gmin].ObjAim.X;
  NowCoor[1] = Aim[Gmin].ObjAim.Y;
  NowCoor[2] = Aim[Gmin].ObjAim.Z;
  obj.X = NowCoor[0] + (Aim[Gmin].AimMovement.X * FlyTime*NumIo[36]);
  obj.Y = NowCoor[1] + (Aim[Gmin].AimMovement.Y * FlyTime*NumIo[36]);
  obj.Z = NowCoor[2] + (Aim[Gmin].AimMovement.Z * FlyTime*NumIo[36]); 
    if (自身动作 == 16 || 自身动作 == 1040 || 自身动作 == 1552 || 自身动作 == 1680 || 自身动作 == 19 || 自身动作 == 17 || 自身动作 == 657 || 自身动作 == 1681 || 自身动作 == 144 || 自身动作 == 5153 || 自身动作 == 35 || 自身动作 == 272 || 自身动作 == 656 || 自身动作 == 145 || 自身动作 == 1313)
            {
 obj.Z -= Aim[Gmin].WodDistance * (g_WeaponPressureDB[MyWeapon] * g_WeaponPressureDX[MyWeapon]) * GetWeaponId(MyWeapon);

}
else if (自身动作 == 672 || 自身动作 == 32 || 自身动作 == 544 || 自身动作 == 35|| 自身动作 == 33 || 自身动作 == 160 || 自身动作 == 288)
{
 obj.Z -= Aim[Gmin].WodDistance * (g_WeaponPressureDB[MyWeapon]* g_WeaponPressurePX[MyWeapon]) * GetWeaponId(MyWeapon);
}
else
{
 obj.Z -= Aim[Gmin].WodDistance * g_WeaponPressureDB[MyWeapon] * GetWeaponId(MyWeapon);//*0.1;
}
  
  float cameras = matrix[3] * obj.X + matrix[7] * obj.Y + matrix[11] * obj.Z + matrix[15];
  
  Vector2A vpvp = WorldToScreen(obj,matrix,cameras);
  瞄准x = vpvp.X;
  瞄准y = vpvp.Y;
  zm_y = vpvp.X;
  zm_x = ScreenX - vpvp.Y;
  float AimDs = sqrt(pow(px - vpvp.X, 2) + pow(py - vpvp.Y, 2));
  
  Aimspeace = NumIo[16];

  if (zm_x <= 0 || zm_x >= ScreenX || zm_y <= 0 || zm_y >= ScreenY)
  {
IsAimLongAim = false;
if (isDown == true)
{
 usleep(1000);
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 // 恢复变量
 Touch_Up();
 // 抬起
 isDown = false;
}
usleep(NumIo[16] * 1000);
continue;
  }
  
  if (ToReticleDistance <= NumIo[3] || AimDs <= NumIo[3])
{
   if (开火判断 != 1) {// 如果未开火
 
  IsAimLongAim = false;
if (isDown == true) {
usleep(1000);

 // 应用随机偏移量
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 
 // 恢复变量 
 Touch_Up();
 // 抬起
 isDown = false;
}
usleep(NumIo[16] * 1000);
  continue;

}


if (isDown == false)
{
 usleep(1000);
 if (NumIo[10] == 0.0f)
 Touch_Down((float)tx, (float)ty);
 else
 Touch_Down(displayInfo.height - (float)tx, displayInfo.width-(float)ty);
 
 isDown = true;
 usleep(1000);
}
if (zm_x > ScrXH)
{
 TargetX = -(ScrXH - zm_x) / Aimspeace;
 if (TargetX + ScrXH > ScrXH * 2)
 {
  TargetX = 0;
 }
}
else if (zm_x < ScrXH)
{
 TargetX = (zm_x - ScrXH) / Aimspeace;
 if (TargetX + ScrXH < 0)
 {
  TargetX = 0;
 }
}

if (zm_y > ScrYH)
{
 TargetY = -(ScrYH - zm_y) / Aimspeace;
 if (TargetY + ScrYH > ScrYH * 2)
 {
  TargetY = 0;
 }
}
else if (zm_y < ScrYH)
{
 TargetY = (zm_y - ScrYH) / Aimspeace;
 if (TargetY + ScrYH < 0)
 {
  TargetY = 0;
 }
}



static float smoothX = 0, smoothY = 0;
static bool smoothInit = false;
float targetX = tx + TargetX;
float targetY = ty + TargetY;

// 平滑初始化
if (!smoothInit) {
    smoothX = targetX;
    smoothY = targetY;
    smoothInit = true;
}

smoothX = smoothX * (1.0f - 0.3) + targetX * 0.3;
smoothY = smoothY * (1.0f - 0.3) + targetY * 0.3;

tx = smoothX;
ty = smoothY;

if (tx >= NumIo[5] + NumIo[7] || tx <= NumIo[5] - NumIo[7]
|| ty >= NumIo[6] + NumIo[7] || ty <= NumIo[6] - NumIo[7])
{
 usleep(1000);
 // 只要滑屏达到了边界，直接还原至中心
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 // 恢复变量
 Touch_Up();
 // 抬起
 usleep(3000);
 // 延迟
 if (!NumIo[10])
 Touch_Down((float)tx, (float)ty);
 else
 Touch_Down(displayInfo.height - (float)tx, displayInfo.width - (float)ty);
 // 按下
 isDown = true;
 
 tx += TargetX;
 ty += TargetY;
 
 usleep(1000);
}

if (!NumIo[10])
Touch_Move((float)tx, (float)ty);
else
Touch_Move(displayInfo.height - (float)tx, displayInfo.width - (float)ty);

isDown = true;

usleep(NumIo[16] * 1000);
  }
  else
  {
IsAimLongAim = false;
if (isDown)
{
 tx = NumIo[5] + distribution_offset(generator);
 ty = NumIo[6] + distribution_offset(generator);

 // 恢复变量
 Touch_Up();
 // 抬起
 isDown = false;
 // 延时
 usleep(NumIo[16] * 1000);
}
  }
 }
}
