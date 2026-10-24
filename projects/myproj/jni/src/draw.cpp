#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "include.h"
#include "aimbot.cpp"
#include "Physx/PhysX.h"
#include "Physx/GameTools.cpp"
#include "Physx/物资id.h"
#include "控件.h"
int abs_ScreenX = 0;
int abs_ScreenY = 0;

pid_t get_name_pid(char* name) {
    FILE* fp;
    pid_t pid;
    char cmd[0x100] = "pidof ";
    strcat(cmd, name);
    fp = popen(cmd,"r");
    fscanf(fp,"%d", &pid);
    pclose(fp);
    return pid;
}
bool isVietnam = false;
 int DrawInt()
{
    char module_name[0x100] = "libUE4.so";
    const char* game_package_names[] = {"com.tencent.ig", "com.rekoo.pubgm", "com.pubg.krmobile", "com.vng.pubgmobile"};
    size_t game_count = sizeof(game_package_names) / sizeof(game_package_names[0]);
    
    for(size_t i = 0; i < game_count; i++) {
        pid = get_name_pid((char*)game_package_names[i]);
        if (pid > 0 && pid < 100000) {
            driver->initialize(pid); // 初始化驱动
            
            // 检查是否是越南版本
            if (strcmp(game_package_names[i], "com.vng.pubgmobile") == 0) {
                isVietnam = true;
            } else {
                isVietnam = false;
            }
            
            break;
        }
    }
    
    UE4 = driver->getModuleBase(module_name); // 获取模块基址
    
    return 0;
}

 void drawMesh(TriangleMeshData* mesh, ImDrawList* Draw) {
    if (!mesh) return;

    for (auto i = 0; i < mesh->Indices.size(); i += 3) {

        const auto v0 = mesh->Vertices[mesh->Indices[i]];
        const auto v1 = mesh->Vertices[mesh->Indices[i + 1]];
        const auto v2 = mesh->Vertices[mesh->Indices[i + 2]];

        Vec2 s0 = GameTools::WorldToScreen(Vec3(v0.x, v0.y, v0.z));
        Vec2 s1 = GameTools::WorldToScreen(Vec3(v1.x, v1.y, v1.z));
        Vec2 s2 = GameTools::WorldToScreen(Vec3(v2.x, v2.y, v2.z));

        if (s0.x > 0 && s0.x < abs_ScreenX && s0.y > 0 && s0.y < abs_ScreenY ||
            s1.x > 0 && s1.x < abs_ScreenX && s1.y > 0 && s1.y < abs_ScreenY ||
            s2.x > 0 && s2.x < abs_ScreenX && s2.y > 0 && s2.y < abs_ScreenY) {
            Draw->AddTriangle(
                ImVec2(s0.x, s0.y),
                ImVec2(s1.x, s1.y),
                ImVec2(s2.x, s2.y),
                ImColor(255, 255, 0,255),
                0.2f
            );
        }
    }
}

#pragma once
#include <cstdint>
 uintptr_t K2_GetActorLocation(uintptr_t SelfPtr,uintptr_t Key);
 uintptr_t K2_GetActorLocation_call(uintptr_t Ptr,uintptr_t Key);
uintptr_t key=0xA8A2FE4;
struct Vector_3D{
float x=0,y=0,z=0;
};
 void exit_decryption();
uintptr_t K2_GetActorLocation_Offset=0xa69be40,K2_GetActorLocation_call_Offset=0xa69a4f0;
Vector_3D K2_GetRelativeLocation_Get(Vector_3D location,uintptr_t Self){
location.x += (K2_GetActorLocation_Offset / 50.0f) * (sinf((uintptr_t)&K2_GetActorLocation_Offset * 0.1234f) * 0.1f + 1.0f) + ((0x719eb1cba ^ K2_GetActorLocation_Offset >> 2) & 0xF) / 100.0f;
location.y += (K2_GetActorLocation_Offset / 50.0f) * (cosf((uintptr_t)&K2_GetActorLocation_Offset * 0.9876f) * 0.1f + 1.0f) - ((0xa7f54ec1 ^ K2_GetActorLocation_Offset << 1) % 0x10) / 100.0f;
location.z += (K2_GetActorLocation_Offset / 50.0f) + tanf(0.0001f) * 100.0f + ((K2_GetActorLocation_Offset * 0x1D3 + 0xc) ^ (K2_GetActorLocation_Offset >> 4)) / 100000.0f;
return driver->read<Vector_3D>(K2_GetActorLocation_call(driver->read<uintptr_t>(K2_GetActorLocation(Self,K2_GetActorLocation_Offset)),K2_GetActorLocation_call_Offset));
return location;
}

 void DrawPlayer(ImDrawList *Draw)
{
    float top,right,left,bottom,x1,top1;
    ImColor 颜色1;
    px = displayInfo.width/2;
    py = displayInfo.height/2;
    // 更新 PhysX 投影用的屏幕尺寸（和 ImGui 坐标中心保持一致）
    GameTools::ScreenSize.x = px;
    GameTools::ScreenSize.y = py;
    if (DrawIo[20])
    {
        Draw->AddCircle({px, py}, NumIo[3],红色,0,0.0f);
    }
    if (DrawIo[21])
    {
        Draw->AddRectFilled(
        {
            0,0
        }
        ,
        {
            px*2, py*2
        }
        ,ImColor(0,0,0,110));
        std::string ssf;
        ssf += "勿放控件xy轴调节";
        auto textSize = ImGui::CalcTextSize(ssf.c_str(), 0, 25);
        Draw->AddRectFilled(
        {
            NumIo[6] - NumIo[7]/2, py*2 - NumIo[5] + NumIo[7]/2
        }
        ,
        {
            NumIo[6] + NumIo[7]/2, py*2 - NumIo[5] - NumIo[7]/2
        }
        ,ImColor(255,0,0,120));
        Draw->AddText(NULL,32,
        {
            NumIo[6] - (textSize.x / 2),py*2 - NumIo[5]
        }
        ,ImColor(255,255,255),ssf.c_str());
    }

    if (isVietnam) {
    Matrix = driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xea04370) + 0xC0) + 0x9D0;//270矩阵
    Gname = driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xe4b2300)+ 0x110);
    Uworld = driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xea2a2e8) + 0x810) + 0x78);// 游戏世
    } else {
    Matrix = driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xe9ea4f0) + 0xC0) + 0x9D0;//270矩阵
    Gname = driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xe498300)+ 0x110);
    Uworld = driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xea10468) + 0x810) + 0x78);// 游戏世界
    }
    Uleve = driver->read<uintptr_t>(Uworld + 0x30);// Uleve
    Arrayaddr = 数组解密(driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(UE4 + 0xe9ea4f0) + 0x30) + 0x20), 0xA0, 0x448);
    countddr = driver->read<int>(Arrayaddr + 0x8);   
    // ⑩ CPU优化：无实体且不画PhysX射线时，跳过所有相机/矩阵读取
    if (countddr == 0 && !DrawIo[86]) {
        return;
    }
    uintptr_t PlayerController = driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(Uworld + 0x38) + 0x78) + 0x30);
    oneself = driver->read<uintptr_t>(PlayerController + 0x28d0);
    Myteam = driver->read<int>(oneself + 0x998);// 自身队伍编号
    Firing = driver->read<int>(oneself + 0x1800);//开火
    Aiming = driver->read<int>(oneself + 0x1134);//开镜
    自身动作 = driver->read<int>(oneself + 0x1058);
    MyWeapon = driver->read<int>(driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(oneself + 0x25b8) + 0x5c8) + 0x1360) + 0x1e0);
    uintptr_t  GameState = driver->read<uintptr_t>(Uworld + 0x428);
    int 剩余玩家 = driver->read<int>(GameState+0xbb4);
    int 本局玩家 = driver->read<int>(GameState+0x818);
    int 剩余队伍 = driver->read<int>(GameState+0xbb8);
    
    uintptr_t AcknowledgedPawn = driver->read<uintptr_t>(PlayerController + 0x528);
    uintptr_t CameraManager = driver->read<uintptr_t>(PlayerController + 0x548);
    uintptr_t CameraCache = CameraManager + 0x520;
    uintptr_t PovPtr  = CameraCache + 0x10;//固定
    Location = driver->read<Vec3>(PovPtr + 0x0);//固定
    Rotator Rotation = driver->read<Rotator>(PovPtr + 0x18);//固定
    Fov = driver->read<float>(PovPtr + 0x24);
    GameTools::WorldMatrix = GameTools::rotatorToMatrix(Rotation);
    
    memset(matrix, 0, 16);;
    driver->read(Matrix, matrix, 16 * 4);
    PlayerCount = 0;
    RobotCount = 0;
    AimCount = 0;
    
    if (DrawIo[86]) {
        // 只在准星方向做一次射线检测并绘制命中的网格
        Vec3 Forward = GameTools::GetForward(Rotation);
        Vec3 End = Location + Forward * 100000.0f; // forward 100000 units
        physx::PxVec3 origin(Location.x, Location.y, Location.z);
        physx::PxVec3 target(End.x, End.y, End.z);

        if (DynamicLoadScene) {
            auto rayHit = DynamicLoadScene->Raycast(origin, target);
            if (rayHit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
                auto HitMesh = DynamicLoadScene->GetGeomeoryData(rayHit.hit.geomID);
                drawMesh(HitMesh, Draw);
            }
        }
        if (HeightFieldScene) {
            auto rayHit = HeightFieldScene->Raycast(origin, target);
            if (rayHit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
                auto HitMesh = HeightFieldScene->GetGeomeoryData(rayHit.hit.geomID);
                drawMesh(HitMesh, Draw);
            }
        }
    }

    for (int i = 0; i < countddr; i++)
    {
        Objaddr = driver->read<uint64_t>(driver->read<uint64_t>(Arrayaddr) + 0x8 * i);
        int ClassID = driver->read<uintptr_t>(Objaddr + 0x18);	// 物资id指针10h
		char ClassName[32] = "";
		long int FNameEntry = driver->read<uintptr_t>(driver->read<uintptr_t>(Gname + (ClassID / 0x4000) * 0x8) + (ClassID % 0x4000) * 0x8);	// 序
		driver->read(FNameEntry + 0xC, ClassName, 64);
        
        Vector3A Z;
        driver->read(driver->read<uintptr_t>(oneself + 0x208) + 0x1c8, &Z, sizeof(Z));// 自己坐标
        // 敌人和物资坐标
        Vector3A D;
        Mesh = driver->read<uintptr_t>(Objaddr + 0x510);
        int 动作 = driver->read<int>(Objaddr + 0x1058);
        if (DrawIo[90]){
        if (driver->read<float>(Objaddr + 0x2B28) == 479.5f) {
        Vector_3D DecryptedPos=K2_GetRelativeLocation_Get(Vector_3D{D.X,D.Y,D.Z},Objaddr);
        D=Vector3A{DecryptedPos.x,DecryptedPos.y,driver->read<float>(Mesh + 0x228)};
        }else{driver->read(driver->read<uintptr_t>(Objaddr + 0x208) + 0x1c8, &D, sizeof(D));if(动作 == 1073741832){D.Z += -88.0f;}else {D.Z += -93.0f;}}
        }else {driver->read(driver->read<uintptr_t>(Objaddr + 0x208) + 0x1c8, &D, sizeof(D));}
        camera = matrix[3] * D.X + matrix[7] * D.Y + matrix[11] * D.Z + matrix[15];
        r_x =px + (matrix[0] * D.X + matrix[4] * D.Y + matrix[8] * D.Z +matrix[12]) / camera * px;
        float 修正Z;
        if (DrawIo[90]){if(动作 == 1073741832){修正Z = D.Z + 88.f;}else{修正Z = D.Z + 93.f;}}else{修正Z = D.Z;}
        r_y = py - (matrix[1] * D.X + matrix[5] * D.Y + matrix[9] * (修正Z - 5) + matrix[13]) / camera * py;
        r_w = py - (matrix[1] * D.X + matrix[5] * D.Y + matrix[9] * (修正Z + 205) + matrix[13]) / camera * py;
        float x = r_x - (r_y - r_w) / 4;
        float y = r_y;
        float w = (r_y - r_w) / 2;
        x1 = x+w/2;
        left = (x + w / 2) - w / 2.6f;
        right = x + w / 1.12f;
        float 距离 = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) * 0.01;
        float Distance =
        sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) * 0.01;
        long int 载具指针 = driver->read<uintptr_t>(Objaddr + 0xc00);
        float 载具耐久 = driver->read<float>(载具指针 + 0x354) / driver->read<float>(载具指针 + 0x350) * 100;
        float 载具油量 = driver->read<float>(载具指针 + 0x43c) / driver->read<float>(载具指针 + 0x438) * 100;
            if(w>0){
            if (DrawIo[91] && (strstr(ClassName, "CharacterDeadInventoryBox_C") != 0 ||
            strstr(ClassName,"EscapePlayerTombBox_C") != 0 ||
            strstr(ClassName,"aracterDeadInventoryBox_C") != 0 ||
            strstr(ClassName,"SuperPeopleDeadBox_C") != 0 ||
            strstr(ClassName,"_TrainingBoxListWrapper_C") != 0 ||
            strstr(ClassName,"PickUpListWrapperActor") != 0) && 距离<=150)
            {
            float 准信距离= sqrt(pow(px - x, 2) + pow(py - y, 2));
            if (准信距离 <= 180.0f)
            {
            uintptr_t 盒内物资指针= driver->read<uintptr_t>(Objaddr + 0x940);
            int 盒内物资= driver->read<uintptr_t>(盒内物资指针 + 0x8);
            if (盒内物资 > 0 && 盒内物资 < 1000)
            {
            uintptr_t 物资数组= 盒内物资指针 + 0x4;            
            float startY = y - 40;            
            int displayedItemCount = 0;            
            for (int i = 0; i < 盒内物资; i++)
            {
            int itemID= driver->read<int>(物资数组 + 0x38 * i);
            if (itemID > 0)
            {
            const char*itemName = GetItems(itemID);
            if (!itemName || strlen(itemName) == 0 || strcmp(itemName, "未知") == 0)
            {
            continue;
            }
            char itemText[128];
            sprintf(itemText,"%s", itemName);            
            float fontSize = 10 *3.829;
            auto itemSize = ImGui::CalcTextSize(itemText, 0, fontSize);
            float textX = x - itemSize.x / 2;
            float textY= y - 20*2.946 - (displayedItemCount * 19.175);
            Draw->AddText(NULL, fontSize, {textX, textY},
            白色,itemText);
            displayedItemCount++;
            }
            }
            }
            }
            }
            }
        if (driver->read<float>(Objaddr + 0x2b28) == 479.5)
        {        
        getUTF8(PlayerName, driver->read<uintptr_t>(Objaddr + 0x960));
        int 手持 = driver->read<int>(driver->read<uintptr_t>(driver->read<uintptr_t>(driver->read<uintptr_t>(Objaddr + 0x25b8) + 0x5c8) + 0x1360) + 0x1e0);
        human = Mesh + 0x210;//+骨骼矩阵
        Bone = driver->read<uintptr_t>(Mesh + 0x990)+ 0x30;
        int BoneCount = driver->read<uint64_t>(Mesh + 0x990 + 0x8);//骨骼
        int TeamID = driver->read<int>(Objaddr + 0x998);
        int State = driver->read<int>(Objaddr + 0x1058);//状态
        float original = (driver->read<float>(Objaddr + 0xe60) / driver->read<float>(Objaddr + 0xe64)) * 100;
        float Health = original ? original : (driver->read<float>(Objaddr + 0x1c00) / driver->read<float>(Objaddr + 0x1cc)) * 100;  
        int isBot = driver->read<int>(Objaddr + 0xa59);
        float angle = driver->read<float>(oneself + 0xd8)-90;
        isBot = (isBot == 16842753 || isBot == 16843009 || isBot == 16843008) ? 1 : 0;
        if (DrawIo[35]) {
        if (isBot== 1)
        {
        continue;   
        }
        }
            if (isBot == 1 || TeamID == 0)
            颜色1 = 白色;//绿色
            else
            颜色1 = 金色;//按队伍分辨颜色
            Vector3A Movement;
            CurrentVehicle = driver->read<uintptr_t>(Objaddr + 0xeb0);
            if (CurrentVehicle)
            {
                driver->read(CurrentVehicle + 0x3870, &Movement, sizeof(Movement));//向量
            }
            else
            {
            driver->read(driver->read<uintptr_t>(Objaddr + 0x208) + 0x2c0, &Movement, sizeof(Movement));// 对象坐标
            }
            Vector2A Radar = rotateCoord(angle, (Z.X - D.X) / (30000/NumIo[17]), (Z.Y - D.Y) / (30000/NumIo[17]));
            if (State == 262144 || State == 262152)
			{
				continue;
			}
			if (driver->read<int>(Objaddr + 0xe7c) == 1)//死亡判断
			{
				continue;
			}
            if (Myteam == TeamID||距离<1)// 不输出自己和队友坐标
            {
                continue;
            }
                  
int 头部=5,胸部=4,盆骨=0,左肩膀=11,右肩膀=32,左手肘=12,右手肘=33,左手腕=63,右手腕=62,左大腿=52,右大腿=56,左膝盖=53,右膝盖=57,左脚腕=54,右脚腕=58; 
if (BoneCount == 67) {左肩膀=13;右肩膀=34;左手肘=14;右手肘=35;左手腕=16;右手腕=37;左大腿=54;右大腿=58;左膝盖=55;右膝盖=59;左脚腕=56;右脚腕=60;} else if (BoneCount == 29) {左肩膀=7;右肩膀=13;左手肘=8;右手肘=14;左手腕=9;右手腕=15;左大腿=18;右大腿=21;左膝盖=19;右膝盖=22;左脚腕=20;右脚腕=23;}

FTransform meshtrans = getBone(human);
if (DrawIo[90])
{
meshtrans.Translation=D;
}
FMatrix c2wMatrix = TransformToMatrix(meshtrans);

FTransform headtrans = getBone(Bone + 头部 * 48);
FMatrix boneMatrix = TransformToMatrix(headtrans);
Vector3A relLocation = MarixToVector(MatrixMulti(boneMatrix, c2wMatrix));
relLocation.Z += 7; // 脖子长度
Vector2A Head = WorldToScreen(relLocation, matrix, camera);

FTransform chesttrans = getBone(Bone + 胸部 * 48);
FMatrix boneMatrix1 = TransformToMatrix(chesttrans);
Vector3A relLocation1 = MarixToVector(MatrixMulti(boneMatrix1, c2wMatrix));
Vector2A Chest = WorldToScreen(relLocation1, matrix, camera);

FTransform pelvistrans = getBone(Bone + 盆骨 * 48);
FMatrix boneMatrix2 = TransformToMatrix(pelvistrans);
Vector3A LrelLocation1 = MarixToVector(MatrixMulti(boneMatrix2, c2wMatrix));
Vector2A Pelvis = WorldToScreen(LrelLocation1, matrix, camera);

FTransform lshtrans = getBone(Bone + 左肩膀 * 48);
FMatrix boneMatrix3 = TransformToMatrix(lshtrans);
Vector3A relLocation2 = MarixToVector(MatrixMulti(boneMatrix3, c2wMatrix));
Vector2A Left_Shoulder = WorldToScreen(relLocation2, matrix, camera);

FTransform rshtrans = getBone(Bone + 右肩膀 * 48);
FMatrix boneMatrix4 = TransformToMatrix(rshtrans);
Vector3A relLocation3 = MarixToVector(MatrixMulti(boneMatrix4, c2wMatrix));
Vector2A Right_Shoulder = WorldToScreen(relLocation3, matrix, camera);

FTransform lelbtrans = getBone(Bone + 左手肘 * 48);
FMatrix boneMatrix5 = TransformToMatrix(lelbtrans);
Vector3A relLocation4 = MarixToVector(MatrixMulti(boneMatrix5, c2wMatrix));
Vector2A Left_Elbow = WorldToScreen(relLocation4, matrix, camera);

FTransform relbtrans = getBone(Bone + 右手肘 * 48);
FMatrix boneMatrix6 = TransformToMatrix(relbtrans);
Vector3A relLocation5 = MarixToVector(MatrixMulti(boneMatrix6, c2wMatrix));
Vector2A Right_Elbow = WorldToScreen(relLocation5, matrix, camera);

FTransform lwtrans = getBone(Bone + 左手腕 * 48);
FMatrix boneMatrix7 = TransformToMatrix(lwtrans);
Vector3A relLocation6 = MarixToVector(MatrixMulti(boneMatrix7, c2wMatrix));
Vector2A Left_Wrist = WorldToScreen(relLocation6, matrix, camera);

FTransform rwtrans = getBone(Bone + 右手腕 * 48);
FMatrix boneMatrix8 = TransformToMatrix(rwtrans);
Vector3A relLocation7 = MarixToVector(MatrixMulti(boneMatrix8, c2wMatrix));
Vector2A Right_Wrist = WorldToScreen(relLocation7, matrix, camera);

FTransform Llshtrans = getBone(Bone + 左大腿 * 48);
FMatrix boneMatrix9 = TransformToMatrix(Llshtrans);
Vector3A LrelLocation2 = MarixToVector(MatrixMulti(boneMatrix9, c2wMatrix));
Vector2A Left_Thigh = WorldToScreen(LrelLocation2, matrix, camera);

FTransform Lrshtrans = getBone(Bone + 右大腿 * 48);
FMatrix boneMatrix10 = TransformToMatrix(Lrshtrans);
Vector3A LrelLocation3 = MarixToVector(MatrixMulti(boneMatrix10, c2wMatrix));
Vector2A Right_Thigh = WorldToScreen(LrelLocation3, matrix, camera);

FTransform Llelbtrans = getBone(Bone + 左膝盖 * 48);
FMatrix boneMatrix11 = TransformToMatrix(Llelbtrans);
Vector3A LrelLocation4 = MarixToVector(MatrixMulti(boneMatrix11, c2wMatrix));
Vector2A Left_Knee = WorldToScreen(LrelLocation4, matrix, camera);

FTransform Lrelbtrans = getBone(Bone + 右膝盖 * 48);
FMatrix boneMatrix12 = TransformToMatrix(Lrelbtrans);
Vector3A LrelLocation5 = MarixToVector(MatrixMulti(boneMatrix12, c2wMatrix));
Vector2A Right_Knee = WorldToScreen(LrelLocation5, matrix, camera);

FTransform Llwtrans = getBone(Bone + 左脚腕 * 48);
FMatrix boneMatrix13 = TransformToMatrix(Llwtrans);
Vector3A LrelLocation6 = MarixToVector(MatrixMulti(boneMatrix13, c2wMatrix));
Vector2A Left_Ankle = WorldToScreen(LrelLocation6, matrix, camera);

FTransform Lrwtrans = getBone(Bone + 右脚腕 * 48);
FMatrix boneMatrix14 = TransformToMatrix(Lrwtrans);
Vector3A LrelLocation7 = MarixToVector(MatrixMulti(boneMatrix14, c2wMatrix));
Vector2A Right_Ankle = WorldToScreen(LrelLocation7, matrix, camera);
            if (LrelLocation6.Z < LrelLocation7.Z)
            {
                LrelLocation7.Z = LrelLocation6.Z;
            }
            top1 = Pelvis.Y - Head.Y;
            top = Pelvis.Y - top1 - w / 5;
            
            if (Left_Ankle.Y < Right_Ankle.Y)
            {
                bottom = Right_Ankle.Y + w / 10;
            }
            else
            {
                bottom = Left_Ankle.Y  + w / 10;
            }
            
          if (DrawIo[7])
    {
    float width = NumIo[1];
    float height = NumIo[2];
    float posX = NumIo[53];
    float posY = NumIo[54];
    float halfWidth = width / 2.0f;
    float halfHeight = height / 2.0f;
    
    ImVec2 topLeft = ImVec2(posX - halfWidth, posY - halfHeight);
    ImVec2 topRight = ImVec2(posX + halfWidth, posY - halfHeight);
    ImVec2 bottomRight = ImVec2(posX + halfWidth, posY + halfHeight);
    ImVec2 bottomLeft = ImVec2(posX - halfWidth, posY + halfHeight);
    
    Draw->AddLine(topLeft, topRight, ImColor(255, 255, 255, 255), 2.0f);
    Draw->AddLine(topRight, bottomRight, ImColor(255, 255, 255, 255), 2.0f);
    Draw->AddLine(bottomRight, bottomLeft, ImColor(255, 255, 255, 255), 2.0f);
    Draw->AddLine(bottomLeft, topLeft, ImColor(255, 255, 255, 255), 2.0f);

    if (Distance < 400)
    {
        // 正确的比例计算 - 雷达显示范围应该与游戏地图匹配
        float radarRange = 62.0f; // 雷达显示的总范围（米）
        float mapScale = (NumIo[17] * 2) / radarRange; // 使用NumIo[17]来计算比例
        
        float relativeX = (D.X - Z.X) * 0.01f;
        float relativeY = (D.Y - Z.Y) * 0.01f;
        
        // 应用正确的比例
        float radarX = posX + (relativeX * mapScale);
        float radarY = posY + (relativeY * mapScale);
        
        ImVec2 point = ImVec2(
            ImClamp(radarX, posX - halfWidth, posX + halfWidth),
            ImClamp(radarY, posY - halfHeight, posY + halfHeight)
        );
        
        ImColor dotColor;
        if (isBot == 1) {
            dotColor = ImColor(255, 255, 255, 255);
        } else {
            dotColor = ImColor(255, 0, 0, 255);
        }
        
        float dotSize = 9.0f;
        
        Draw->AddCircleFilled(point, dotSize, dotColor);

        char distanceStr[32];
        sprintf(distanceStr, "%.0f   m", Distance);

        ImVec2 textSize = ImGui::CalcTextSize(distanceStr);
        
        // 文字位置调整
        ImVec2 textPos = ImVec2(point.x - textSize.x / 2, point.y + dotSize - 33);

        textPos.x = ImClamp(textPos.x, posX - halfWidth, posX + halfWidth - textSize.x);
        textPos.y = ImClamp(textPos.y, posY - halfHeight, posY + halfHeight - textSize.y);

        ImColor textColor = ImColor(255, 255, 255, 255);
        Draw->AddText(textPos, textColor, distanceStr);
    }
}
       

            if(w>0)
            {
                bool needBoneData = DrawIo[89] && (DrawIo[3] || DrawIo[20]);
                std::vector<Vector3A> BoneWorld;
                std::vector<bool> BoneVisible;
                if (needBoneData) {
                    BoneWorld.resize(15);
                    BoneVisible.resize(15);
                    BoneWorld[0]  = relLocation;      // 头
                    BoneWorld[1]  = relLocation1;     // 胸
                    BoneWorld[2]  = LrelLocation1;    // 骨盆
                    BoneWorld[3]  = relLocation2;     // 左肩
                    BoneWorld[4]  = relLocation3;     // 右肩
                    BoneWorld[5]  = relLocation4;     // 左肘
                    BoneWorld[6]  = relLocation5;     // 右肘
                    BoneWorld[7]  = relLocation6;     // 左手腕
                    BoneWorld[8]  = relLocation7;     // 右手腕
                    BoneWorld[9]  = LrelLocation2;    // 左大腿
                    BoneWorld[10] = LrelLocation3;    // 右大腿
                    BoneWorld[11] = LrelLocation4;    // 左膝
                    BoneWorld[12] = LrelLocation5;    // 右膝
                    BoneWorld[13] = LrelLocation6;    // 左脚踝
                    BoneWorld[14] = LrelLocation7;    // 右脚踝
                    Vec3 PovLocation(Location.x, Location.y, Location.z);
                    for (int j = 0; j < 15 && j < (int)BoneWorld.size(); j++) {
                        try {
                            Vec3 target(BoneWorld[j].X, BoneWorld[j].Y, BoneWorld[j].Z);
                            BoneVisible[j] = !LineTrace::LineTraceSingle(PovLocation, target);
                        } catch (...) { BoneVisible[j] = false; }  /* 单次异常不崩溃，默认可见 */
                    }
                }


                if (DrawIo[20])
                {
                    bool isDistanceValid = Distance <= NumIo[98] || 
                        (NumIo[98] < Distance && Distance < NumIo[30] && (Aiming == 1 || Aiming == 257));
                    bool isDrawIo22Valid = DrawIo[22] ? Health > 0 : true;
                    bool isDrawIo23Valid = DrawIo[23] ? (isBot != 1) : true;

                    if (isDistanceValid && isDrawIo22Valid && isDrawIo23Valid)
                    {
                        if (DrawIo[89]) {
                            // 漏打模式：只打露出的骨骼，头优先；全部被遮挡则不实现自瞄（不加入 Aim）
                            int targetIndex = -1;
                            if (BoneVisible[0]) targetIndex = 0;
                            else {
                                int order[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14};
                                for (int idx : order) {
                                    if (BoneVisible[idx]) { targetIndex = idx; break; }
                                }
                            }
                            if (targetIndex != -1) {
                                strcpy(Aim[AimCount].Name, PlayerName);
                                Aim[AimCount].WodDistance = Distance;
                                Aim[AimCount].AimMovement = Movement;
                                Aim[AimCount].ObjAim = BoneWorld[targetIndex];
                                Vector2A scr = WorldToScreen(BoneWorld[targetIndex], matrix, camera);
                                Aim[AimCount].ScreenDistance = sqrtf(powf(px - scr.X, 2.0f) + powf(py - scr.Y, 2.0f));
                                AimCount++;
                            }
                        } else {
                            // 普通模式：按 NumIo[8] 选头/胸/骨盆，不做 PhysX 判断
                            strcpy(Aim[AimCount].Name, PlayerName);
                            Aim[AimCount].WodDistance = Distance;
                            Aim[AimCount].AimMovement = Movement;
                            if (NumIo[8] == 1.0f) {
                                Aim[AimCount].ObjAim = relLocation;
                                Aim[AimCount].ScreenDistance = sqrtf(powf(px - Head.X, 2.0f) + powf(py - Head.Y, 2.0f));
                            } else if (NumIo[8] == 2.0f || NumIo[8] == 0.0f) {
                                Aim[AimCount].ObjAim = relLocation1;
                                Aim[AimCount].ScreenDistance = sqrtf(powf(px - Chest.X, 2.0f) + powf(py - Chest.Y, 2.0f));
                            } else if (NumIo[8] == 3.0f) {
                                Aim[AimCount].ObjAim = LrelLocation1;
                                Aim[AimCount].ScreenDistance = sqrtf(powf(px - Pelvis.X, 2.0f) + powf(py - Pelvis.Y, 2.0f));
                            } else {
                                Aim[AimCount].ObjAim = relLocation1;
                                Aim[AimCount].ScreenDistance = sqrtf(powf(px - Chest.X, 2.0f) + powf(py - Chest.Y, 2.0f));
                            }
                            AimCount++;
                        }
                    }
                }
                  if (DrawIo[2])
                {
                    if (isBot == 1)
                    {
                    Draw->AddLine({px , 120},{Head.X, top} ,人机骨骼颜色,{1.0});
                    }else{
                    Draw->AddLine({px , 120},{Head.X, top} ,骨骼颜色,{1.0});
                    }
                }

                          
                if (DrawIo[3]) {
                    ImColor fixedColor = (isBot == 1) ? 人机骨骼颜色 : 骨骼颜色;
                    auto segColor = [&](int boneIndex, bool isBotEnemy) -> ImColor {
                        if (DrawIo[89] && needBoneData && BoneVisible[boneIndex]) return 绿色;
                        return isBotEnemy ? 人机骨骼颜色 : 骨骼颜色;
                    };

                    // 漏打开启：露出的段绿色，被挡的用固定色；漏打关闭：全部用固定色
                    Draw->AddCircle({Head.X , Head.Y}, w/14, DrawIo[89] && needBoneData ? segColor(0, isBot == 1) : fixedColor, 0);
                    Draw->AddLine({Head.X, Head.Y}, {Chest.X, Chest.Y}, DrawIo[89] && needBoneData ? segColor(1, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Chest.X, Chest.Y}, {Pelvis.X, Pelvis.Y}, DrawIo[89] && needBoneData ? segColor(2, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Chest.X, Chest.Y}, {Left_Shoulder.X,Left_Shoulder.Y}, DrawIo[89] && needBoneData ? segColor(3, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Chest.X, Chest.Y}, {Right_Shoulder.X,Right_Shoulder.Y}, DrawIo[89] && needBoneData ? segColor(4, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Left_Shoulder.X, Left_Shoulder.Y}, {Left_Elbow.X,Left_Elbow.Y}, DrawIo[89] && needBoneData ? segColor(5, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Right_Shoulder.X, Right_Shoulder.Y},{Right_Elbow.X, Right_Elbow.Y}, DrawIo[89] && needBoneData ? segColor(6, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Left_Elbow.X, Left_Elbow.Y}, {Left_Wrist.X,Left_Wrist.Y}, DrawIo[89] && needBoneData ? segColor(7, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Right_Elbow.X, Right_Elbow.Y}, {Right_Wrist.X,Right_Wrist.Y}, DrawIo[89] && needBoneData ? segColor(8, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Pelvis.X, Pelvis.Y}, {Left_Thigh.X, Left_Thigh.Y}, DrawIo[89] && needBoneData ? segColor(9, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Pelvis.X, Pelvis.Y}, {Right_Thigh.X,Right_Thigh.Y}, DrawIo[89] && needBoneData ? segColor(10, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Left_Thigh.X, Left_Thigh.Y}, {Left_Knee.X,Left_Knee.Y}, DrawIo[89] && needBoneData ? segColor(11, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Right_Thigh.X, Right_Thigh.Y}, {Right_Knee.X,Right_Knee.Y}, DrawIo[89] && needBoneData ? segColor(12, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Left_Knee.X, Left_Knee.Y}, {Left_Ankle.X,Left_Ankle.Y}, DrawIo[89] && needBoneData ? segColor(13, isBot == 1) : fixedColor,{NumIo[32]});
                    Draw->AddLine({Right_Knee.X, Right_Knee.Y}, {Right_Ankle.X,Right_Ankle.Y}, DrawIo[89] && needBoneData ? segColor(14, isBot == 1) : fixedColor,{NumIo[32]});
                }
                if (DrawIo[5]) {
                    std::string s;
                    s += std::to_string((int) Distance);
                    s += "M";
                    auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                    Draw->AddText(NULL,50,{x1-30, y+w-15}, 白色,s.c_str());
                }
    if (DrawIo[6]) {
if (!isBot) {
DrawHealth({Head.X, top }, w * 1, Health, TeamID, PlayerName,arr[TeamID % length]);
} else {
DrawHealth({Head.X, top }, w * 1, Health, TeamID, "人机",白色);
}
}
                if (DrawIo[11]) {
  string s;
  s += GetHolGunItem(手持);
  auto textSize = ImGui::GetFont()->CalcTextSizeA(25, FLT_MAX, -1, s.c_str(), NULL, NULL); 
  ImGui::GetForegroundDrawList()->AddText(NULL, 25.f+12.6, { Head.X - (textSize.x / 2)-6.3, top - 65 - (textSize.y / 2)-6.3 }, ImColor(255,255,255,255), s.c_str());                    
  }
            }
            if (isBot == 1 || TeamID == 0)
            RobotCount++;
            else
            PlayerCount++;
        }else{
        char *vp_name;
                if (w < 0 || Distance > 800)
                {
                    continue;
                }
                if (DrawIo[52]){
                if(宝箱(ClassName, &vp_name))
                {
                绘制字体描边(60.0f, x, y, 红色, ".");
                }
                }
                if (DrawIo[51]){
                //手雷
                if(手雷(ClassName, &vp_name))
                {
                    std::string goods;
                    goods += vp_name;
                    goods += " (";
                    goods += std::to_string((int)Distance);
                    goods += "m)";
                    auto textSize = ImGui::CalcTextSize(goods.c_str(), 0, 30);
                    绘制字体描边(40, x, y, 红色, goods.c_str());
                }
                }
                //显示物资
                if (DrawIo[50]){
                if(物资(ClassName, &vp_name))
                {
                    std::string goods;
                    goods += vp_name;                   
                    auto textSize = ImGui::CalcTextSize(goods.c_str(), 0, 30);
                    绘制字体描边(40, x, y, 白色, goods.c_str());
                }
                }
                //类名
                if(DrawIo[90]){
                    std::string 类名;
                    类名 += ClassName;                   
                    auto textSize = ImGui::CalcTextSize(类名.c_str(), 0, 30);
                    绘制字体描边(40, x, y, 白色, 类名.c_str());
                }
                //显示载具
                if (DrawIo[9]){
                if(载具(ClassName, &vp_name))
                {                          
                    std::string goods;   
                    goods += vp_name;
                    goods += " ";
                    goods += std::to_string((int)Distance);
                    goods += " m";
                    auto textSize = ImGui::CalcTextSize(goods.c_str(), 0, 30);
绘制字体描边(30.0, x - 25, y, 白色, goods.c_str());
// ===== 位置与尺寸调整 =====
float barStartX = x - 30;          // 横向基准不变
float barStartY = y + textSize.y - 12;   // 往上移 8 像素
float barWidth  = 100;             // 总宽度缩到 100
float barHeight = 6;
float spacing   = 3;
float cornerRadius = 2.0f;
float shrink = 5.0f;               // 左右各缩 5 像素
float usableWidth = barWidth - 2 * shrink;
float vehicleCenterX = r_x;
float vehicleCenterY = r_y - w/2;
float distanceToVehicle = sqrt(pow(px - vehicleCenterX, 2) + pow(py - vehicleCenterY, 2));
if (distanceToVehicle <= 110.0f) 
{
    // ===== 油量条（白色）=====
    // 背景
    ImGui::GetBackgroundDrawList()->AddRect(
        {barStartX + shrink, barStartY},
        {barStartX + shrink + usableWidth, barStartY + barHeight},
        ImColor(0, 0, 0, 255),
        cornerRadius
    );

    // 填充
    if (载具油量 > 0)
    {
        float fillWidth = (载具油量 / 100.0f) * usableWidth;
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            {barStartX + shrink + 1, barStartY + 1},
            {barStartX + shrink + 1 + fillWidth, barStartY + barHeight - 1},
            ImColor(255, 255, 255, 255),
            cornerRadius - 1
        );
    }

    // ===== 耐久条（红色）=====
    // 背景
    ImGui::GetBackgroundDrawList()->AddRect(
        {barStartX + shrink, barStartY + barHeight + spacing},
        {barStartX + shrink + usableWidth, barStartY + barHeight * 2 + spacing},
        ImColor(0, 0, 0, 255),
        cornerRadius
    );

    // 填充
    if (载具耐久 > 0)
    {
        float fillWidth = (载具耐久 / 100.0f) * usableWidth;
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            {barStartX + shrink + 1, barStartY + barHeight + spacing + 1},
            {barStartX + shrink + 1 + fillWidth, barStartY + barHeight * 2 + spacing - 1},
            ImColor(255, 0, 0, 255),
            cornerRadius - 1
        );
    }
}
                }
                }
                                                 
                                                 
        }      
    }
         if (DrawIo[55]) {
     std::string str;
str = "全图人数:";
str += to_string((int)剩余玩家);
str += "\n真实玩家:";
str += to_string((int)本局玩家);
str += "\n队伍数:";
str += to_string((int)剩余队伍);
ImVec2 textPos = ImVec2(px* 0.015, py * 1.14);
Draw->AddText(NULL, 40, textPos, ImColor(255,255,255,255), str.c_str());
     }

    MaxPlayerCount = AimCount;
    static float prevTotal = 0.f;                       // 记住上一帧值
    float curTotal = (float)(PlayerCount + RobotCount); // 当前值
    prevTotal = ImLerp(prevTotal, curTotal, ImGui::GetIO().DeltaTime * 8.f); // 平滑
    int displayCount = (int)ImFloor(prevTotal + 0.5f);  // 真正显示

    const float xFactor = (displayCount < 10) ? 1.0f : 0.8f;
    ImVec2 centerPos = ImVec2(px - 30 * 2.6f * xFactor, 110);

    ImColor textColor;
    if (PlayerCount > 0 && RobotCount > 0) {
        textColor = ImColor(255, 0, 0);
    } else if (PlayerCount > 0) {
        textColor = ImColor(255, 0, 0);
    } else if (RobotCount > 0) {
        textColor = ImColor(255, 255, 0);
    } else {
        textColor = ImColor(255, 255, 255);
    }

    std::string countStr = std::to_string(displayCount);
    auto countTextSize = ImGui::CalcTextSize(countStr.c_str(), nullptr, false);

    // Y 坐标也跟着插值即可产生上下滑动
    float topY = 60.f;
    float deltaY = (curTotal - prevTotal) * 20.f;        // 20 可调整速度
    ImVec2 countPos = ImVec2(px - 20, topY - deltaY);

    Draw->AddText(nullptr, 100, countPos, textColor, countStr.c_str());
}