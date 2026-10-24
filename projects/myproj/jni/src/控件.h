
 void DrawHealth(ImVec2 box, float w, float iHealth, uint32_t TeamID, char* name, ImColor 血颜色)
{
float cornerRadius = 8.0f; 
float x = box.x;
float y = box.y;
float 差值;
if (TeamID > 99) TeamID = TeamID/2;
if (TeamID > 9) 差值 = 3; else 差值 = 0;
char TeamText[50];
sprintf(TeamText, "%d.%s", TeamID, name); 
char Teamid[50];
sprintf(Teamid, "%d", TeamID); 

ImColor healthColor = iHealth == 100.0f ? ImColor(255, 255, 255, 155) :
                   iHealth >= 75.0f ? ImColor(255, 255, 255, 255) :
                   iHealth >= 50.0f ? ImColor(255, 150, 150, 255) :
                   iHealth >= 25.0f ? ImColor(255, 80, 80, 255) :
                                      ImColor(255, 0, 0, 255);
ImGui::GetBackgroundDrawList()->AddRect({x - 56, y - 23}, {x + 56, y - 12}, ImColor(0,0,0,255), cornerRadius);
if (iHealth > 0)
ImGui::GetBackgroundDrawList()->AddRectFilled({x - 55, y - 22}, {x - 55 + (iHealth * 110 / 100), y - 13},血颜色, cornerRadius);
auto textSize_信息 = ImGui::GetFont()->CalcTextSizeA(27, FLT_MAX, -1, TeamText, NULL, NULL);
绘制字体描边(25+12.6, x - (textSize_信息.x / 2)+-10.5, box.y - 34 - (textSize_信息.y / 2)+-8.0, ImVec4{255,255,255,255}, TeamText);
}//结束大括号