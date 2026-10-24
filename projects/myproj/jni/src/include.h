#include <cstring>
static bool 宝箱(char* gname,char** name);
static bool 物资(char* gname,char** name);
static bool 手雷(char* gname,char** name);
static bool 载具(char* gname,char** name);

 bool 宝箱(char* gname,char** name)
{
if (strstr(gname, "ItemBox_Lua_Lv1_C") != nullptr)//敌人盒子
    {
        *name = "1级物资箱";
        return true;
    }
    if (strstr(gname, "ItemBox_Lua_Lv2_C") != nullptr)//敌人盒子
    {
        *name = "2级物资箱";
        return true;
    }
    if (strstr(gname, "BP_DC_HighLevel_TE_ItemBox_Lua_Lv3_2_C") != nullptr)//敌人盒子
    {
        *name = "4级物资箱";

        return true;
    }
    if (strstr(gname, "ItemBox_Lua_Lv3_C") != nullptr)//敌人盒子
    {
        *name = "3级物资箱";
        return true;
    }
    if (strstr(gname, "ItemBox_Lua_Lv4_C") != nullptr)//敌人盒子
    {
        *name = "4级物资箱";
        return true;
    }
    if (strstr(gname, "ItemBox_Lua_Lv5_C") != nullptr)//敌人盒子
    {
        *name = "5级物资箱";
        return true;
    }
    if (strstr(gname, "ItemBox_Lua_Lv6_C") != nullptr)//敌人盒子
    {
        *name = "6级物资箱";
        return true;
    }
    return false;
}

 bool 物资(char* gname,char** name)
{
        if (strstr(gname, "PickUpListWrapperActor") != 0)
        {
            *name="盒子";
            //*color=beibao_color;
            return true;           // 盒子
        }
        if (strstr(gname, "PlayerDeadBox") != 0)
        {
            *name="盒子";
          //  *color=beibao_color;
            return true;           // 盒子
        }
       if (strstr(gname, "XTPlayerDeadInventoryBox_C") != 0)
        {
            *name="盒子";
           // *color=beibao_color;
            return true;           // 盒子
        }
    if (strstr(gname, "BP_Pistol_Flaregun_Wrapper_C") != 0)
        {
            *name="信号枪";
            return true;           
        }
        if (strstr(gname, "BP_Ammo_Flare_Pickup_C") != 0)
        {
            *name="信号枪子弹";
        }
        if (strstr(gname, "BP_Pistol_Flaregun_C") != 0)
        {
            *name="信号枪";
            return true;           
        }
    if (strstr(gname, "BP_revivalAED_Pickup_C") != 0)
        {
            *name="自救器";
            return true;           
        }
    return false;
}




// 手雷预警
 bool 手雷(char* gname,char** name)
{
if(strcmp(gname,"BP_Grenade_Smoke_C")==0)
    {
        *name="烟雾弹!";
                return true;
    }
    if(strcmp(gname,"BP_Projectile_SmokeBomb_C")==0)
    {
        *name="烟雾弹!";
                return true;
    }
    if(strcmp(gname,"BP_Grenade_Burn_C")==0)
    {
        *name="燃烧瓶!";
                return true;
    }
    if(strcmp(gname,"BP_Projectile_BurnGrenade_C")==0)
    {
        *name="燃烧瓶!";
                return true;
    }
if(strcmp(gname,"BP_Grenade_Shoulei_C")==0)
    {
        *name="手雷!";
                return true;
    }
    if(strcmp(gname,"BP_Projectile_FragGrenade_C")==0)
    {
        *name="手雷!";
                return true;
    }
    return false;
}
// 获取载具
 bool 载具(char* gname,char** name)
{
    if(strcmp(gname,"VH_BRDM_C")==0)
    {
        *name="装甲车";        
        return true;
    }
    if(strcmp(gname,"VH_Horse_C")==0||strcmp(gname,"VH_Horse_1_C")==0||strcmp(gname,"VH_Horse_2_C")==0||strcmp(gname,"VH_Horse_3_C")==0||strcmp(gname,"VH_Horse_4_C")==0)
    {
        *name="马";        
        return true;
    }
    if(strcmp(gname,"VH_Scooter_C")==0)
    {
        *name="小绵羊";        
        return true;
    }
    if(strcmp(gname,"VH_Motorcycle_C")==0||strcmp(gname,"VH_Motorcycle_1_C")==0)
    {
        *name="摩托车";        
        return true;
    }
    if(strcmp(gname,"VH_MotorcycleCart_1_C")==0||strcmp(gname,"VH_MotorcycleCart_C")==0||strstr(gname,"VH_MotorcycleCart_C_1_Pooled")!= nullptr)
    {
        *name="三轮摩托";        
        return true;
    }
    if(strcmp(gname,"VH_Snowmobile_C")==0)
    {
        *name="雪地摩托";        
        return true;
    }
    if(strcmp(gname,"BP_VH_Tuk_C")==0||strcmp(gname,"BP_VH_Tuk_1_C")==0)
    {
        *name="三轮摩托";       
        return true;
    }
    if(strstr(gname,"Buggy")!= nullptr||strstr(gname,"BP_VH_Buggy_C")!= nullptr||strstr(gname,"BP_VH_Buggy_2_C")!= nullptr||strstr(gname,"BP_VH_Buggy_3_C")!= nullptr||strstr(gname,"BP_VH_Buggy_C_1_Pooled")!= nullptr)
    {
        *name="蹦蹦车";        
        return true;
    }
    if(strstr(gname,"Dacia")!= nullptr||strstr(gname,"VH_Dacia_3_New_C")!= nullptr||strstr(gname,"VH_Dacia_C")!= nullptr||strstr(gname,"VH_Dacia_C_1_Pooled")!= nullptr||strstr(gname,"VH_Dacia_2_C")!= nullptr||strstr(gname,"VH_Dacia_2_C_1_Pooled")!= nullptr||strstr(gname,"VH_Dacia_3_C")!= nullptr||strstr(gname,"VH_Dacia_4_C")!= nullptr||strstr(gname,"VH_Dacia_4_C_1_Pooled")!= nullptr||strstr(gname,"VH_Dacia_3_C_1_Pooled")!= nullptr)
    {
        *name="轿车";        
        return true;
    }
    if(strstr(gname,"UAZ")!= nullptr||strcmp(gname,"VH_UAZ01_C")==0||strcmp(gname,"VH_UAZ01_C_1_Pooled")==0||strcmp(gname,"VH_UAZ03_C_1_Pooled")==0||strcmp(gname,"VH_UAZ03_C")==0)
    {
        *name="吉普车";        
        return true;
    }
    if(strstr(gname,"_PickUp")!= nullptr)
    {
        *name="皮卡车";        
        return true;
    }
    if(strstr(gname,"PickUp_07_C")!= nullptr)
    {
        *name="皮卡车越野";        
        return true;
    }
    if(strstr(gname,"VH_Motorglider_C")!= nullptr)
    {
        *name="滑翔机";        
        return true;
    }
    if(strstr(gname,"VH_UTV_C")!= nullptr)
    {
        *name="UTV车";        
        return true;
    }
    if(strstr(gname,"BP_VH_Bigfoot_C")!= nullptr)
    {
        *name="大脚车";        
        return true;
    }

    if(strstr(gname,"BP_CoupeRB_Base_C")!= nullptr||strstr(gname,"VH_CoupeRB_1_C")!= nullptr||strstr(gname,"VH_CoupeRB_1_C_1_Pooled")!= nullptr||strstr(gname,"BP_CoupeRB_Base_C")!= nullptr)
    {
        *name="黄跑车";        
        return true;
    }
        
    if(strstr(gname,"VH_ATV1_C")!= nullptr)
    {
        *name="小越野车";        
        return true;
    }
    if(strstr(gname,"VH_Snowbike_C")!= nullptr)
    {
        *name="雪地车";        
        return true;
    }
    if(strstr(gname,"Rony")!= nullptr||strstr(gname,"Rony_01_C")!= nullptr)
    {
        *name="皮卡车";        
        return true;
    }
    if(strstr(gname,"Mirado")!= nullptr||strstr(gname,"Mirado_open_4_C")!= nullptr||strstr(gname,"Mirado_open_1_C")!= nullptr||strstr(gname,"Mirado_open_2_C")!= nullptr||strstr(gname,"Mirado_open_3_C")!= nullptr)
    {
        *name="跑车";        
        return true;
    }
    if(strstr(gname,"MiniBus")!= nullptr||strstr(gname,"VH_MiniBus_01_C")!= nullptr)
    {
        *name="迷你巴士";        
        return true;
    }
    if(strstr(gname,"PG117")!= nullptr||strstr(gname,"VH_PG117_C")!= nullptr||strstr(gname,"VH_PG117_Sl_C")!= nullptr||strstr(gname,"VH_PG117_C_1_Pooled")!= nullptr||strstr(gname,"VH_AmphibiousBoat_C")!= nullptr||strstr(gname,"AquaRail_1_C")!= nullptr)
    {
        *name="快艇";        
        return true;
    }
    if(strstr(gname,"AquaRail")!= nullptr)
    {
        *name="摩托艇";        
        return true;
    }
    if(strstr(gname,"PickUp_07_C")!= nullptr)
    {
        *name="旅行越野";        
        return true;
    }
    if(strstr(gname,"VH_Drift_001_New_C")!= nullptr)
    {
        *name="拉力塞车";        
        return true;
    }
    if(strstr(gname,"VH_StoreBus_C")!= nullptr||strstr(gname,"VH_StoreBus_C_1_Pooled")!= nullptr)
    {
        *name="购物车";        
        return true;
    }
    if(strstr(gname,"VH_StationWagon_New_C")!= nullptr)
    {
        *name="旅行车";        
        return true;
    }
    if(strstr(gname,"VH_Picobus_C")!= nullptr)
    {
        *name="电动巴士";        
        return true;
    }
    if(strstr(gname,"VH_Blanc_C")!= nullptr)
    {
        *name="布朗SUV";        
        return true;
    }
    return false;
}

