#ifndef SSAGEPARUDERS_NATIVE_SURFACE_H
bool g_need_verify = false;
#define SSAGEPARUDERS_NATIVE_SURFACE_H


#include "Draw.h"
#include "TouchHelperA.h"
#include <tool.h>
MyTextureData 七八;
MyTextureData bg_tex;  // 自定义背景图纹理
char bg_path[256] = "";  // 背景图路径，用户可替换
bool bg_loaded = false;
bool DrawIo[200], 初始化 = true,Firing,IsAimLongAim = false;

bool BallSwitch = true, MemuSwitch = true,IsWin = true;

float NumIo[200],matrix[16] = { 0 }, angle, camera, r_x, r_y, r_w,ToReticleDistance,瞄准x,瞄准y,zm_x,zm_y,Fov,Aimspeace;

int style_idx2 = 0, FPS, style_idx3 = 0,intIo[200],NumIos[200],Myteam,countddr,PlayerCount,RobotCount,自身动作,WorldDistance,Aiming,MyWeapon,AimCount = 0,MaxPlayerCount = 0,Gmin=-1;

long int UE4, Gname, Uworld, Arrayaddr, Matrix, oneself, Objaddr, Object, weapon,handheld, Mesh, human, Bone, Uleve,CurrentVehicle;

ImGuiWindow *g_window = nullptr;
ImGuiIO *Io = nullptr;
ImGuiIO & io = ImGui::GetIO();
FILE *numSave = nullptr;
void win1();
ImColor 红色=ImColor(255,0,0,255);
ImColor 白色=ImColor(255,255,255,255);
ImColor 绿色=ImColor(0,255,0,255);
ImColor 粉色=ImColor(226,145,163,255);
ImColor 紫色=ImColor(15,67,255,255);
ImColor 黄色=ImColor(200,255,0,255);
ImColor 黑色=ImColor(0,0,0,255);
ImColor 蓝色=ImColor(0,167,241,255);
ImColor 棕色=ImColor(144,144,144,255);
ImColor 灰色=ImColor(10,40,40,205);
ImColor 空白=ImColor(242,255,255);
ImColor BoneColor = ImColor(255,0,0,255);
ImColor TouchingColor = ImColor(255, 0, 0, 150);
ImColor 金色=ImColor(250, 180, 60);
ImColor 骨骼颜色 = 红色;
ImColor 人机骨骼颜色 = 白色;
char extra[100];
char PlayerName[100];
char AimName[32];
struct AimStruct
{
    Vector3A ObjAim;  
    Vector3A AimMovement;  
    float ScreenDistance = 0;
    float WodDistance = 0; 
	char Name[32];
}
Aim[100];
struct ActorsEncryption {
uint32_t Enc_1,Enc_2;
uint32_t Enc_3,Enc_4;
};
struct Encryption_Chunk {
uint32_t val_1,val_2;
uint32_t val_3,val_4;
};
struct Actors {
uint64_t Enc_1,Enc_2;
uint64_t Enc_3,Enc_4;
};
struct Chunk {
uint32_t val_1,val_2,val_3,val_4;
uint32_t val_5,val_6,val_7,val_8;
};
uint64_t 数组解密(uint64_t uLevel,int Actors_Offset,int EncryptedActors_Offset) {
if(uLevel<0x10000000)
return 0;
if(driver->read<uint64_t>(uLevel+Actors_Offset)>0)
return uLevel+Actors_Offset;
if(driver->read<uint64_t>(uLevel+EncryptedActors_Offset)>0)
return uLevel+EncryptedActors_Offset;
auto AActors=driver->read<Actors>(uLevel+EncryptedActors_Offset+0x10);
if(AActors.Enc_1>0) {
auto Enc=driver->read<Chunk>(AActors.Enc_1+0x80);
return (((driver->read<uint8_t>(AActors.Enc_1+Enc.val_1)
| (driver->read<uint8_t>(AActors.Enc_1+Enc.val_2) << 8))
| (driver->read<uint8_t>(AActors.Enc_1+Enc.val_3) << 0x10)) & 0xFFFFFF
| ((uint64_t)driver->read<uint8_t>(AActors.Enc_1+Enc.val_4) << 0x18)
| ((uint64_t)driver->read<uint8_t>(AActors.Enc_1+Enc.val_5) << 0x20)) & 0xFFFF00FFFFFFFFFF
| ((uint64_t)driver->read<uint8_t>(AActors.Enc_1+Enc.val_6) << 0x28)
| ((uint64_t)driver->read<uint8_t>(AActors.Enc_1+Enc.val_7) << 0x30)
| ((uint64_t)driver->read<uint8_t>(AActors.Enc_1+Enc.val_8) << 0x38);
}
else if(AActors.Enc_2>0) {
auto Lost_Actors=driver->read<uint64_t>(AActors.Enc_2);
if(Lost_Actors>0) {
return (uint16_t)(Lost_Actors-0x400) & 0xFF00
| (uint8_t)(Lost_Actors-0x04)
| (Lost_Actors+0xFC0000) & 0xFF0000
| (Lost_Actors-0x4000000) & 0xFF000000
| (Lost_Actors+0xFC00000000) & 0xFF00000000
| (Lost_Actors+0xFC0000000000) & 0xFF0000000000
| (Lost_Actors+0xFC000000000000) & 0xFF000000000000
| (Lost_Actors-0x400000000000000) & 0xFF00000000000000;
}
}
else if(AActors.Enc_3>0) {
auto Lost_Actors=driver->read<uint64_t>(AActors.Enc_3);
if(Lost_Actors>0) {
return (Lost_Actors >> 0x38) | (Lost_Actors << (64-0x38));
}
}
else if(AActors.Enc_4>0) {
auto Lost_Actors=driver->read<uint64_t>(AActors.Enc_4);
if(Lost_Actors>0) {
return Lost_Actors ^ 0xCDCD00;
}
}
return 0;
}
#endif//SSAGEPARUDERS_NATIVE_SURFACE_H