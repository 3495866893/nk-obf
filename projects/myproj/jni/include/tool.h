#ifndef NATIVESURFACE_MEMREAD_H
#define NATIVESURFACE_MEMREAD_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <malloc.h>
#include <math.h>
#include <thread>
#include <iostream>
#include <sys/stat.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>
#include <locale>
#include <string>
#include <codecvt>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/uio.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include "driver.h"
using namespace std;
std::vector<std::string> lines;
	// 定义圆周率
#define PI 3.141592653589793238
typedef unsigned short UTF16;
typedef char UTF8;
float px,py;
// 算法 结构体 //
struct Vector2A
{
	float X;
	float Y;

	  Vector2A()
	{
		this->X = 0;
		this->Y = 0;
	}

	Vector2A(float x, float y)
	{
		this->X = x;
		this->Y = y;
	}
};
struct Vector3A
{
	float X;
	float Y;
	float Z;

	  Vector3A()
	{
		this->X = 0;
		this->Y = 0;
		this->Z = 0;
	}

	Vector3A(float x, float y, float z)
	{
		this->X = x;
		this->Y = y;
		this->Z = z;
	}

};
struct D2DVector
{
	float X;
	float Y;
};

struct D3DVector
{
	float X;
	float Y;
	float Z;
};
struct D4DVector
{
	float X;
	float Y;
	float Z;
	float W;
};


struct FMatrix
{
	float M[4][4];
};





struct FTransform
{
	D4DVector Rotation;
	Vector3A Translation;
	float chunk;
	Vector3A Scale3D;
};






Vector3A MarixToVector(FMatrix matrix)//
{
	return Vector3A(matrix.M[3][0], matrix.M[3][1], matrix.M[3][2]);
}

FMatrix MatrixMulti(FMatrix m1, FMatrix m2)//
{
	FMatrix matrix = FMatrix();
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < 4; k++)
			{
				matrix.M[i][j] += m1.M[i][k] * m2.M[k][j];
			}
		}
	}
	return matrix;
}

FMatrix TransformToMatrix(FTransform transform)//
{
	FMatrix matrix;
	matrix.M[3][0] = transform.Translation.X;
	matrix.M[3][1] = transform.Translation.Y;
	matrix.M[3][2] = transform.Translation.Z;
	float x2 = transform.Rotation.X + transform.Rotation.X;
	float y2 = transform.Rotation.Y + transform.Rotation.Y;
	float z2 = transform.Rotation.Z + transform.Rotation.Z;
	float xx2 = transform.Rotation.X * x2;
	float yy2 = transform.Rotation.Y * y2;
	float zz2 = transform.Rotation.Z * z2;
	matrix.M[0][0] = (1 - (yy2 + zz2)) * transform.Scale3D.X;
	matrix.M[1][1] = (1 - (xx2 + zz2)) * transform.Scale3D.Y;
	matrix.M[2][2] = (1 - (xx2 + yy2)) * transform.Scale3D.Z;
	float yz2 = transform.Rotation.Y * z2;
	float wx2 = transform.Rotation.W * x2;
	matrix.M[2][1] = (yz2 - wx2) * transform.Scale3D.Z;
	matrix.M[1][2] = (yz2 + wx2) * transform.Scale3D.Y;
	float xy2 = transform.Rotation.X * y2;
	float wz2 = transform.Rotation.W * z2;
	matrix.M[1][0] = (xy2 - wz2) * transform.Scale3D.Y;
	matrix.M[0][1] = (xy2 + wz2) * transform.Scale3D.X;
	float xz2 = transform.Rotation.X * z2;
	float wy2 = transform.Rotation.W * y2;
	matrix.M[2][0] = (xz2 + wy2) * transform.Scale3D.Z;
	matrix.M[0][2] = (xz2 - wy2) * transform.Scale3D.X;
	matrix.M[0][3] = 0;
	matrix.M[1][3] = 0;
	matrix.M[2][3] = 0;
	matrix.M[3][3] = 1;
	return matrix;
}

FTransform getBone(unsigned long addr)
{
    FTransform transform;
    driver->read(addr, &transform, 4 * 11);
    return transform;
}

// 读取字符信息
 void getUTF8(char *buf, unsigned long namepy)
{
	unsigned short buf16[32] = {0};
	driver->read(namepy, buf16, 28);
	unsigned short *pTempUTF16 = buf16;
	char *pTempUTF8 = buf;
	char *pUTF8End = pTempUTF8 + 32;
	while (pTempUTF16 < pTempUTF16 + 28)
	{
		if (*pTempUTF16 <= 0x007F && pTempUTF8 + 1 < pUTF8End)
		{
			*pTempUTF8++ = (char)*pTempUTF16;
		}
		else if (*pTempUTF16 >= 0x0080 && *pTempUTF16 <= 0x07FF && pTempUTF8 + 2 < pUTF8End)
		{
			*pTempUTF8++ = (*pTempUTF16 >> 6) | 0xC0;
			*pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
		}
		else if (*pTempUTF16 >= 0x0800 && *pTempUTF16 <= 0xFFFF && pTempUTF8 + 3 < pUTF8End)
		{
			*pTempUTF8++ = (*pTempUTF16 >> 12) | 0xE0;
			*pTempUTF8++ = ((*pTempUTF16 >> 6) & 0x3F) | 0x80;
			*pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
		}
		else
		{
			break;
		}
		pTempUTF16++;
	}
}








// 计算旋转坐标
Vector2A rotateCoord(float angle, float objRadar_x, float objRadar_y)
{
	Vector2A radarCoordinate;
	float s = sin(angle * PI / 180);
	float c = cos(angle * PI / 180);
	radarCoordinate.X = objRadar_x * c + objRadar_y * s;
	radarCoordinate.Y = -objRadar_x * s + objRadar_y * c;
	return radarCoordinate;
}
//世界坐标转屏幕坐标
Vector2A WorldToScreen(Vector3A obj, float matrix[16], float ViewW)//
{
	float x =
		px + (matrix[0] * obj.X + matrix[4] * obj.Y + matrix[8] * obj.Z + matrix[12]) / ViewW * px;
	float y =
		py - (matrix[1] * obj.X + matrix[5] * obj.Y + matrix[9] * obj.Z + matrix[13]) / ViewW * py;

	return Vector2A(x, y);
}






static float tm1=127/255.f;
static ImVec4 arr[] = {
        {144/255.f, 238/255.f, 144/255.f, tm1},
        {135/255.f, 206/255.f, 255/255.f, tm1},
        {255/255.f, 0/255.f,   0/255.f,   tm1},
        {0/255.f,   255/255.f, 0/255.f,   tm1},
        {0/255.f,   255/255.f, 127/255.f, tm1},
        {255/255.f, 182/255.f, 193/255.f, tm1},
        {218/255.f, 112/255.f, 214/255.f, tm1},
        {248/255.f, 248/255.f, 255/255.f, tm1},
        {0/255.f,   255/255.f, 255/255.f, tm1},
        {255/255.f, 165/255.f, 0/255.f,   tm1},
        {153/255.f, 204/255.f, 255/255.f, tm1},
        {204/255.f, 255/255.f, 153/255.f, tm1},
        {255/255.f, 255/255.f, 153/255.f, tm1},
        {255/255.f, 153/255.f, 153/255.f, tm1},
        {153/255.f, 153/255.f, 204/255.f, tm1},
        {204/255.f, 204/255.f, 204/255.f, tm1},
        {102/255.f, 204/255.f, 153/255.f, tm1},
        {255/255.f, 102/255.f, 0/255.f,   tm1},
        {102/255.f, 204/255.f, 204/255.f, tm1},
        {153/255.f, 204/255.f, 255/255.f, tm1}
};
static int length=sizeof(arr)/16;
 float get_3D_Distance(float Self_x, float Self_y, float Self_z, float Object_x, float Object_y,
					  float Object_z)
{
	float x, y, z;
	x = Self_x - Object_x;
	y = Self_y - Object_y;
	z = Self_z - Object_z;
	// 求平方根
	return (float)(sqrt(x * x + y * y + z * z));
}
 void DrawPlayerBox(ImDrawList *Draw, float left,float right, float bottom, float top, float x, float y, ImColor color)
{
	float LineSize = 1.0f;
	// x距离，y距离
	float xd = x - left; float yd = y - top;
	
	// 左上角
	Draw->AddLine({left,top},{left - xd*2,top}, color, {LineSize});
	Draw->AddLine({left,top},{left,top + yd/3}, color, {LineSize});
	
	// 右上角
	Draw->AddLine({right,top},{right + xd*2,top}, color, {LineSize});
	Draw->AddLine({right, top},{right,top + yd/3}, color, {LineSize});
	
	// 左下角
	Draw->AddLine({left,bottom},{left - xd*2,bottom}, color, {LineSize});
	Draw->AddLine({left,bottom},{left,bottom - yd/3}, color, {LineSize});
	
	// 右下角
	Draw->AddLine({right,bottom},{right + xd*2,bottom},color, {LineSize});
	Draw->AddLine({right,bottom},{right,bottom - yd/3}, color, {LineSize});
	
}

 void Draw3DBox(float Camera, Vector3A Max,Vector3A Min,ImColor color,float matrix[16])
{
	float LineSize = 1.5f;
	
    Min.X = Max.X - 50.f;
    Min.Y = Max.Y - 50.f; // foot
    Min.Z = Min.Z - 10.f;

    Max.X = Max.X + 50.f;
    Max.Y = Max.Y + 50.f; // head
    Max.Z = Max.Z + 50.f;

    Vector3A v1, v2, v3, v4, v5, v6, v7, v8;
    v1 = Min;v8 = Max;v2 = Min;v3 = Min;v4 = Min;

    // 转换
    v2.X = Max.X;
    v3.Y = Max.Y;
    v4.X = Max.X;
    v4.Y = Max.Y;

    v5 = v1;
    v5.Z = Max.Z;
    v6 = v2;
    v6.Z = Max.Z;
    v7 = v3;
    v7.Z = Max.Z;

    Vector2A vv1, vv2, vv3, vv4, vv5, vv6, vv7, vv8;
    
    vv1 = WorldToScreen(v1, matrix,Camera);
    vv2 = WorldToScreen(v2, matrix,Camera);
    vv3 = WorldToScreen(v3, matrix,Camera);
    vv4 = WorldToScreen(v4, matrix,Camera);
    vv5 = WorldToScreen(v5, matrix,Camera);
    vv6 = WorldToScreen(v6, matrix,Camera);
    vv7 = WorldToScreen(v7, matrix,Camera);
    vv8 = WorldToScreen(v8, matrix,Camera);
        
    ImGui::GetForegroundDrawList()->AddLine({vv1.X, vv1.Y}, {vv2.X, vv2.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv1.X, vv1.Y}, {vv3.X, vv3.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv1.X, vv1.Y}, {vv5.X, vv5.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv2.X, vv2.Y}, {vv4.X, vv4.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv2.X, vv2.Y}, {vv6.X, vv6.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv3.X, vv3.Y}, {vv4.X, vv4.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv3.X, vv3.Y}, {vv7.X, vv7.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv4.X, vv4.Y}, {vv8.X, vv8.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv5.X, vv5.Y}, {vv6.X, vv6.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv5.X, vv5.Y}, {vv7.X, vv7.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv7.X, vv7.Y}, {vv8.X, vv8.Y}, color, {LineSize});
    ImGui::GetForegroundDrawList()->AddLine({vv6.X, vv6.Y}, {vv8.X, vv8.Y}, color, {LineSize});
}
 void 绘制加粗文本(float size, float x, float y, ImColor color, ImColor color1, const char* str)
{
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x-0.1, y-0.1), color1, str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x+0.1, y+0.1), color1, str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size, ImVec2(x, y), color, str);
}
 void 绘制字体描边(float size,int x, int y, ImVec4 color, const char* str)
{
    ImGui::GetBackgroundDrawList()->AddText(NULL, size,ImVec2(x + 1, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size,ImVec2(x - 0.1, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size,ImVec2(x, y + 1), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size,ImVec2(x, y - 1), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), str);
    ImGui::GetBackgroundDrawList()->AddText(NULL, size,ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), str);
}
 void DrawCircularImage(ImVec2 center, float radius, ImTextureID icon, ImU32 color, bool isCircular = true)
{
    ImGui::SetCursorPos({ 0, 180 });
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // 计算图片的显示范围
    ImVec2 image_min = { center.x - radius, center.y - radius };
    ImVec2 image_max = { center.x + radius, center.y + radius };

    if (isCircular)
    {
        // 使用 AddImageRounded 方法绘制圆形图片
        draw_list->AddImageRounded(icon, image_min, image_max, ImVec2(0, 0), ImVec2(1, 1), color, radius, ImDrawFlags_RoundCornersAll);
    }
    else
    {
        // 直接绘制普通矩形图片
        draw_list->AddImage(icon, image_min, image_max, ImVec2(0, 0), ImVec2(1, 1), color);
    }
}
//获取手持
char *GetHolGunItem(int BoxId)
{
    switch (BoxId)
    {
       
//投掷爆炸物
    

    case 0:
      return "拳头";
      break;
      
  case 9807001:
 return "大砍刀";
 break;
 case 9807002:
 return "撬棍";
 break;
 case 9807003:
 return "镰刀";
 break;
 case 9807004:
 return "平底锅";
 break;
 case 9807005:
 return "火焰刀";
 break;
case 101001:
return "AKM";
break;
case 101901:
return "AKM-XT";
break;
case 1010011:
return "AKM-破损";
break;
case 1010012:
return "AKM-修复";
break;
case 1010013:
return "AKM-完好";
break;
case 1010014:
return "AKM-改进";
break;
case 1010015:
return "AKM-精制";
break;
case 1010016:
return "AKM-独眼";
break;
case 1010017:
return "AKM-钢铁";
break;
case 1010019:
return "AKM";
break;
case 101002:
return "M16A4";
break;
case 1010021:
return "M16A4-破损";
break;
case 1010022:
return "M16A4-修复";
break;
case 1010023:
return "M16A4-完好";
break;
case 1010024:
return "M16A4-改进";
break;
case 1010025:
return "M16A4-精制";
break;
case 101003:
return "SCAR-L";
break;
case 101903:
return "SCAR-XT";
break;
case 1010031:
return "SCAR-L-破损";
break;
case 1010032:
return "SCAR-L-修复";
break;
case 1010033:
return "SCAR-L-完好";
break;
case 1010034:
return "SCAR-L-改进";
break;
case 1010035:
return "SCAR-L-精制";
break;
case 1010036:
return "SCAR-L-独眼";
break;
case 1010037:
return "SCAR-L-钢铁";
break;
case 1010039:
return "SCAR-L";
break;
case 101004:
return "M416";
break;
case 1010041:
return "M416-破损";
break;
case 1010042:
return "M416-修复";
break;
case 1010043:
return "M416-完好";
break;
case 1010044:
return "M416-改进";
break;
case 1010045:
return "M416-精制";
break;
case 1010046:
return "M416-独眼";
break;
case 1010047:
return "M416-钢铁";
break;
case 1010049:
return "M416";
break;
case 101005:
return "GROZA";
break;
case 1010051:
return "GROZA-破损";
break;
case 1010052:
return "GROZA-修复";
break;
case 1010053:
return "GROZA-完好";
break;
case 1010054:
return "GROZA-改进";
break;
case 1010055:
return "GROZA-精制";
break;
case 1010056:
return "GROZA-独眼";
break;
case 1010057:
return "GROZA-钢铁";
break;
case 1010059:
return "GROZA";
break;
case 101006:
return "AUG";
break;
case 1010061:
return "AUG-破损";
break;
case 1010062:
return "AUG-修复";
break;
case 1010063:
return "AUG-完好";
break;
case 1010064:
return "AUG-改进";
break;
case 1010065:
return "AUG-精制";
break;
case 1010066:
return "AUG-独眼";
break;
case 1010067:
return "AUG-钢铁";
break;
case 101007:
return "QBZ";
break;
case 1010071:
return "QBZ-破损";
break;
case 1010072:
return "QBZ-修复";
break;
case 1010073:
return "QBZ-完好";
break;
case 1010074:
return "QBZ-改进";
break;
case 1010075:
return "QBZ-精制";
break;
case 1010076:
return "QBZ-独眼";
break;
case 1010077:
return "QBZ-钢铁";
break;
case 101008:
return "M762";
break;
case 101908:
return "M762-XT";
break;
case 1010081:
return "M762-破损";
break;
case 1010082:
return "M762-修复";
break;
case 1010083:
return "M762-完好";
break;
case 1010084:
return "M762-改进";
break;
case 1010085:
return "M762-精制";
break;
case 1010086:
return "M762-独眼";
break;
case 1010087:
return "M762-钢铁";
break;
case 1010089:
return "M762";
break;
case 101009:
return "Mk47";
break;
case 1010091:
return "Mk47-破损";
break;
case 1010092:
return "Mk47-修复";
break;
case 1010093:
return "Mk47-完好";
break;
case 1010094:
return "Mk47-改进";
break;
case 1010095:
return "Mk47-精制";
break;
case 101010:
return "G36C";
break;
case 1010101:
return "G36C-破损";
break;
case 1010102:
return "G36C-修复";
break;
case 1010103:
return "G36C-完好";
break;
case 1010104:
return "G36C-改进";
break;
case 1010105:
return "G36C-精制";
break;
case 1010106:
return "G36C-独眼";
break;
case 1010107:
return "G36C-钢铁";
break;
case 101012:
return "蜜獾";
break;
case 1010121:
return "蜜獾-破损";
break;
case 1010122:
return "蜜獾-修复";
break;
case 1010123:
return "蜜獾-完好";
break;
case 1010124:
return "蜜獾-改进";
break;
case 1010125:
return "蜜獾-精制";
break;
case 1010126:
return "蜜獾-独眼";
break;
case 1010127:
return "蜜獾-钢铁";
break;
case 101100:
return "FAMAS";
break;
case 1011001:
return "FAMAS-破损";
break;
case 1011002:
return "FAMAS-修复";
break;
case 1011003:
return "FAMAS-完好";
break;
case 1011004:
return "FAMAS-改进";
break;
case 1011005:
return "FAMAS-精制";
break;
case 1011006:
return "FAMAS-独眼";
break;
case 1011007:
return "FAMAS-钢铁";
break;
case 101102:
return "ACE32";
break;
case 1011021:
return "ACE32-破损";
break;
case 1011022:
return "ACE32-修复";
break;
case 1011023:
return "ACE32-完好";
break;
case 1011024:
return "ACE32-改进";
break;
case 1011025:
return "ACE32-精制";
break;
case 1011026:
return "ACE32-独眼";
break;
case 1011027:
return "ACE32-钢铁";
break;
case 102001:
return "UZI";
break;
case 102901:
return "UZI-XT";
break;
case 1020011:
return "UZI-破损";
break;
case 1020012:
return "UZI-修复";
break;
case 1020013:
return "UZI-完好";
break;
case 1020014:
return "UZI-改进";
break;
case 1020015:
return "UZI-精制";
break;
case 102002:
return "UMP45";
break;
case 1020021:
return "UMP45-破损";
break;
case 1020022:
return "UMP45-修复";
break;
case 1020023:
return "UMP45-完好";
break;
case 1020024:
return "UMP45-改进";
break;
case 1020025:
return "UMP45-精制";
break;
case 1020029:
return "UMP45";
break;
case 102003:
return "Vector";
break;
case 102903:
return "Vector-XT";
break;
case 1020031:
return "Vector-破损";
break;
case 1020032:
return "Vector-修复";
break;
case 1020033:
return "Vector-完好";
break;
case 1020034:
return "Vector-改进";
break;
case 1020035:
return "Vector-精制";
break;
case 1020036:
return "Vector-独眼";
break;
case 1020037:
return "Vector-钢铁";
break;
case 102004:
return "汤姆逊";
break;
case 1020041:
return "汤姆逊-破损";
break;
case 1020042:
return "汤姆逊-修复";
break;
case 1020043:
return "汤姆逊-完好";
break;
case 1020044:
return "汤姆逊-改进";
break;
case 1020045:
return "汤姆逊-精制";
break;
case 102005:
return "野牛";
break;
case 1020051:
return "野牛-破损";
break;
case 1020052:
return "野牛-修复";
break;
case 1020053:
return "野牛-完好";
break;
case 1020054:
return "野牛-改进";
break;
case 1020055:
return "野牛-精制";
break;
case 1020059:
return "野牛";
break;
case 102007:
return "MP5K";
break;
case 1020071:
return "MP5K-破损";
break;
case 1020072:
return "MP5K-修复";
break;
case 1020073:
return "MP5K-完好";
break;
case 1020074:
return "MP5K-改进";
break;
case 1020075:
return "MP5K-精制";
break;
case 1020076:
return "MP5K-独眼";
break;
case 1020077:
return "MP5K-钢铁";
break;
case 102105:
return "P90";
break;
case 1021051:
return "P90-破损";
break;
case 1021052:
return "P90-修复";
break;
case 1021053:
return "P90-完好";
break;
case 1021054:
return "P90-改进";
break;
case 1021055:
return "P90-精制";
break;
case 103001:
return "Kar98K";
break;
case 103901:
return "Kar98K-XT";
break;
case 1030011:
return "Kar98K-破损";
break;
case 1030012:
return "Kar98K-修复";
break;
case 1030013:
return "Kar98K-完好";
break;
case 1030014:
return "Kar98K-改进";
break;
case 1030015:
return "Kar98K-精制";
break;
case 1030019:
return "Kar98K";
break;
case 103002:
return "M24";
break;
case 103902:
return "M24-XT";
break;
case 1030021:
return "M24-破损";
break;
case 1030022:
return "M24-修复";
break;
case 1030023:
return "M24-完好";
break;
case 1030024:
return "M24-改进";
break;
case 1030025:
return "M24-精制";
break;
case 1030026:
return "M24-独眼";
break;
case 1030027:
return "M24-钢铁";
break;
case 103003:
return "AWM";
break;
case 103903:
return "AWM";
break;
case 1030031:
return "AWM-破损";
break;
case 1030032:
return "AWM-修复";
break;
case 1030033:
return "AWM-完好";
break;
case 1030034:
return "AWM-改进";
break;
case 1030035:
return "AWM-精制";
break;
case 1030036:
return "AWM-独眼";
break;
case 1030037:
return "AWM-钢铁";
break;
case 1030039:
return "AWM";
break;
case 103004:
return "SKS";
break;
case 1030041:
return "SKS-破损";
break;
case 1030042:
return "SKS-修复";
break;
case 1030043:
return "SKS-完好";
break;
case 1030044:
return "SKS-改进";
break;
case 1030045:
return "SKS-精制";
break;
case 1030046:
return "SKS-独眼";
break;
case 1030047:
return "SKS-钢铁";
break;
case 103005:
return "VSS";
break;
case 1030051:
return "VSS-破损";
break;
case 1030052:
return "VSS-修复";
break;
case 1030053:
return "VSS-完好";
break;
case 1030054:
return "VSS-改进";
break;
case 1030055:
return "VSS-精制";
break;
case 1030059:
return "VSS";
break;
case 103006:
return "Mini14";
break;
case 1030061:
return "Mini14-破损";
break;
case 1030062:
return "Mini14-修复";
break;
case 1030063:
return "Mini14-完好";
break;
case 1030064:
return "Mini14-改进";
break;
case 1030065:
return "Mini14-精制";
break;
case 1030069:
return "Mini14";
break;
case 1031011:
return "提卡-破损";
break;
case 1031012:
return "提卡-修复";
break;
case 1031013:
return "提卡-完好";
break;
case 1031014:
return "提卡-改进";
break;
case 1031015:
return "提卡-精制";
break;
case 103007:
return "Mk14";
break;
case 1030071:
return "Mk14-破损";
break;
case 1030072:
return "Mk14-修复";
break;
case 1030073:
return "Mk14-完好";
break;
case 1030074:
return "Mk14-改进";
break;
case 1030075:
return "Mk14-精制";
break;
case 1030076:
return "Mk14-独眼";
break;
case 1030077:
return "Mk14-钢铁";
break;
case 103008:
return "Win94";
break;
case 1030081:
return "Win94-破损";
break;
case 1030082:
return "Win94-修复";
break;
case 1030083:
return "Win94-完好";
break;
case 1030084:
return "Win94-改进";
break;
case 1030085:
return "Win94-精制";
break;
case 103009:
return "SLR";
break;
case 1030091:
return "SLR-破损";
break;
case 1030092:
return "SLR-修复";
break;
case 1030093:
return "SLR-完好";
break;
case 1030094:
return "SLR-改进";
break;
case 1030095:
return "SLR-精制";
break;
case 1030096:
return "SLR-独眼";
break;
case 1030097:
return "SLR-钢铁";
break;
case 1030099:
return "SLR";
break;
case 103010:
return "QBU";
break;
case 1030101:
return "QBU-破损";
break;
case 1030102:
return "QBU-修复";
break;
case 1030103:
return "QBU-完好";
break;
case 1030104:
return "QBU-改进";
break;
case 1030105:
return "QBU-精制";
break;
case 103012:
return "AMR";
break;
case 1030121:
return "AMR-破损";
break;
case 1030122:
return "AMR-修复";
break;
case 1030123:
return "AMR-完好";
break;
case 1030124:
return "AMR-改进";
break;
case 1030125:
return "AMR-精制";
break;
case 1030126:
return "AMR-独眼";
break;
case 1030127:
return "AMR-钢铁";
break;
case 103100:
return "Mk12";
break;
case 1031001:
return "Mk12-破损";
break;
case 1031002:
return "Mk12-修复";
break;
case 1031003:
return "Mk12-完好";
break;
case 1031004:
return "Mk12-改进";
break;
case 1031005:
return "Mk12-精制";
break;
case 1031006:
return "Mk12-独眼";
break;
case 1031007:
return "Mk12-钢铁";
break;
case 103101:
return "提卡";
break;
case 104001:
return "S686";
break;
case 1040011:
return "S686-破损";
break;
case 1040012:
return "S686-修复";
break;
case 1040013:
return "S686-完好";
break;
case 1040014:
return "S686-改进";
break;
case 1040015:
return "S686-精制";
break;
case 1040022:
return "S1897-修复";
break;
case 1040023:
return "S1897-完好";
break;
case 1040024:
return "S1897-改进";
break;
case 1040025:
return "S1897-精制";
break;
case 104003:
return "S12K";
break;
case 1040031:
return "S12K-破损";
break;
case 1040032:
return "S12K-修复";
break;
case 1040033:
return "S12K-完好";
break;
case 1040034:
return "S12K-改进";
break;
case 1040035:
return "S12K-精制";
break;
case 1040039:
return "S12K";
break;
case 104004:
return "DBS";
break;
case 1040041:
return "DBS-破损";
break;
case 1040042:
return "DBS-修复";
break;
case 1040043:
return "DBS-完好";
break;
case 1040044:
return "DBS-改进";
break;
case 1040045:
return "DBS-精制";
break;
case 1040049:
return "DBS";
break;
case 104102:
return "NS2000";
break;
case 1041021:
return "NS2000-破损";
break;
case 1041022:
return "NS2000-修复";
break;
case 1041023:
return "NS2000-完好";
break;
case 1041024:
return "NS2000-改进";
break;
case 1041025:
return "NS2000-精制";
break;
case 105001:
return "M249";
break;
case 1050011:
return "M249-破损";
break;
case 1050012:
return "M249-修复";
break;
case 1050013:
return "M249-完好";
break;
case 1050014:
return "M249-改进";
break;
case 1050015:
return "M249-精制";
break;
case 1050016:
return "M249-独眼";
break;
case 1050017:
return "M249-钢铁";
break;
case 105002:
return "DP-28";
break;
case 1050021:
return "DP-28-破损";
break;
case 1050022:
return "DP-28-修复";
break;
case 1050023:
return "DP-28-完好";
break;
case 1050024:
return "DP-28-改进";
break;
case 1050025:
return "DP-28-精制";
break;
case 1050029:
return "DP-28";
break;
case 105010:
return "MG3";
break;
case 1050101:
return "MG3-破损";
break;
case 1050102:
return "MG3-修复";
break;
case 1050103:
return "MG3-完好";
break;
case 1050104:
return "MG3-改进";
break;
case 1050105:
return "MG3-精制";
break;
case 1050106:
return "MG3-独眼";
break;
case 1050107:
return "MG3-钢铁";
break;
case 1050109:
return "MG3";
break;
case 106001:
return "P92";
break;
case 1060011:
return "P92-破损";
break;
case 1060012:
return "P92-修复";
break;
case 1060013:
return "P92-完好";
break;
case 1060014:
return "P92-改进";
break;
case 1060015:
return "P92-精制";
break;
case 1060019:
return "P92";
break;
case 106002:
return "P1911";
break;
case 1060021:
return "P1911-破损";
break;
case 1060022:
return "P1911-修复";
break;
case 1060023:
return "P1911-完好";
break;
case 1060024:
return "P1911-改进";
break;
case 1060025:
return "P1911-精制";
break;
case 106003:
return "R1895";
break;
case 1060031:
return "R1895-破损";
break;
case 1060032:
return "R1895-修复";
break;
case 1060033:
return "R1895-完好";
break;
case 1060034:
return "R1895-改进";
break;
case 1060035:
return "R1895-精制";
break;
case 106004:
return "P18C";
break;
case 1060041:
return "P18C-破损";
break;
case 1060042:
return "P18C-修复";
break;
case 1060043:
return "P18C-完好";
break;
case 1060044:
return "P18C-改进";
break;
case 1060045:
return "P18C-精制";
break;
case 1060049:
return "P18C";
break;
case 106005:
return "R45";
break;
case 1060051:
return "R45-破损";
break;
case 1060052:
return "R45-修复";
break;
case 1060053:
return "R45-完好";
break;
case 1060054:
return "R45-改进";
break;
case 1060055:
return "R45-精制";
break;
case 106006:
return "短管";
break;
case 1060061:
return "短管-破损";
break;
case 1060062:
return "短管-修复";
break;
case 1060063:
return "短管-完好";
break;
case 1060064:
return "短管-改进";
break;
case 1060065:
return "短管-精制";
break;
case 1060069:
return "短管";
break;
case 106008:
return "蝎式";
break;
case 1060081:
return "蝎式-破损";
break;
case 1060082:
return "蝎式-修复";
break;
case 1060083:
return "蝎式-完好";
break;
case 1060084:
return "蝎式-改进";
break;
case 1060085:
return "蝎式-精制";
break;case 1060089:
return "蝎式";
break;
case 106010:
return "沙漠之鹰";
break;
case 1060101:
return "沙漠之鹰-破损";
break;
case 1060102:
return "沙漠之鹰-修复";
break;
case 1060103:
return "沙漠之鹰-完好";
break;
case 1060104:
return "沙漠之鹰-改进";
break;
case 1060105:
return "沙漠之鹰-精制";
break;
case 1060109:
return "沙漠之鹰-指挥官";
break;
case 107001:
return "十字弩";
break;
case 1070011:
return "十字弩-破损";
break;
case 1070012:
return "十字弩-修复";
break;
case 1070013:
return "十字弩-完好";
break;
case 1070014:
return "十字弩-改进";
break;
case 1070015:
return "十字弩-精制";
break;
case 106103:
return "信号枪";
break;
case 106106:
return "信号枪-空";
break;
case 106007:
return "信号枪";
break;
case 1060071:
return "撤离信号枪";
break;
break;
case 101011:
return "AC-VAL";
break;
case 101101:
return "ASMAbakan";
break;
case 103011:
return "莫辛纳甘";
break;
case 104100:
return "SPAS-12";
break;
case 104101:
return "M1014";
break;
case 106908:
return "Vz61-XT";
break;
case 105004:
return "火焰喷射器";
break;
break;
case 107003:
return "防爆盾";
break;
case 107007:
return "战术弩";
break;
case 107008:
return "爆炸猎弓";
break;
case 107009:
return "爆炸猎弓";
break;
case 107020:
return "爆炸弩";
break;
case 602001:
return "震爆弹";
break;
case 602002:
return "烟雾弹";
break;
case 602003:
return "燃烧瓶";
break;
case 602004:
return "手榴弹";
break;
case 108001:
return "大砍刀";
break;
case 1080011:
return "大砍刀";
break;
case 108002:
return "撬棍";
break;
case 108003:
return "镰刀";
break;
case 108004:
return "平底锅";
break;
case 1080041:
return "平底锅";
break;
case 108005:
return "军用匕首";
break;
case 1080051:
return "军用匕首";
break;
case 602005:
return "苹果";
      break;

             
    default:
      return "特殊武器";
      break;
  }
    return nullptr;
}

#include <unordered_map>
#include <fstream>

// ========== 原始数据存储 ==========
std::unordered_map<int, float> g_WeaponPressureDB_Data;  // 战力
std::unordered_map<int, float> g_WeaponPressureDX_Data;  // 蹲下压枪
std::unordered_map<int, float> g_WeaponPressurePX_Data;  // 趴下压枪

struct WeaponPressureConfig {
    float db;
    float dx;
    float px;
};
std::unordered_map<int, WeaponPressureConfig> g_WeaponConfigs;

// ========== 安全包装类 ==========
class SafeWeaponMap {
private:
    std::unordered_map<int, float>& data;
    float defaultValue;  // 每个map有自己的默认值
    
public:
    SafeWeaponMap(std::unordered_map<int, float>& mapData, float defaultVal) 
        : data(mapData), defaultValue(defaultVal) {}
    
    // 读取操作：不存在时返回对应的默认值
    float operator[](int weaponID) const {
        auto it = data.find(weaponID);
        if (it != data.end()) {
            return it->second;
        }
        return defaultValue;
    }
    
    // 写入操作：不存在时初始化为对应的默认值
    float& operator[](int weaponID) {
        if (data.find(weaponID) == data.end()) {
            data[weaponID] = defaultValue;
        }
        return data[weaponID];
    }
    
    // 清空操作
    void clear() {
        data.clear();
    }
};

// ========== 创建全局对象，分别设置不同的默认值 ==========
SafeWeaponMap g_WeaponPressureDB(g_WeaponPressureDB_Data, 1.2f);   // 站立默认1.0
SafeWeaponMap g_WeaponPressureDX(g_WeaponPressureDX_Data, 0.7f);   // 蹲下默认0.8
SafeWeaponMap g_WeaponPressurePX(g_WeaponPressurePX_Data, 0.4f);   // 趴下默认0.5

// ========== 保存配置 ==========
 void SaveWeaponConfig() {
    std::ofstream configFile("/data/独立武器配置.cfg");
    if (configFile.is_open()) {
        for (const auto& [weaponID, pressure] : g_WeaponPressureDB_Data) {
            auto itDX = g_WeaponPressureDX_Data.find(weaponID);
            auto itPX = g_WeaponPressurePX_Data.find(weaponID);
            
            float dx = (itDX != g_WeaponPressureDX_Data.end()) ? itDX->second : 0.7f;
            float px = (itPX != g_WeaponPressurePX_Data.end()) ? itPX->second : 0.4f;
            
            configFile << weaponID << " "
                       << pressure << " "
                       << dx << " "
                       << px << "\n";
        }
        configFile.close();
    }
}

// ========== 加载配置 ==========
 void LoadWeaponConfig() {
    std::ifstream configFile("/data/独立武器配置.cfg");
    if (configFile.is_open()) {
        g_WeaponPressureDB_Data.clear();
        g_WeaponPressureDX_Data.clear();
        g_WeaponPressurePX_Data.clear();
        g_WeaponConfigs.clear();

        int weaponID;
        float db, dx, px;
        while (configFile >> weaponID >> db >> dx >> px) {
            g_WeaponPressureDB_Data[weaponID] = db;
            g_WeaponPressureDX_Data[weaponID] = dx;
            g_WeaponPressurePX_Data[weaponID] = px;
            g_WeaponConfigs[weaponID] = {db, dx, px};
        }
        configFile.close();
    }
}
ImColor ColorArr[100];
ImColor RandomColor()
{
    int R, G, B, A = 150;
    R = (random() % 255);
    G = (random() % 255);
    B = (random() % 255);
    return ImColor(R, G, B, A);
}
 void ColorInitialization()
{
    for(int i = 0; i < 100; i++) {
        ColorArr[i] = RandomColor();
    }
}

ImColor TeamColor(int Num)
{
    if(Num < 99 && Num > 0) {
        return ColorArr[Num];
    } else {
        return ImColor(67, 205, 128, 150);
    }
}
#endif
