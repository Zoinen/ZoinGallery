#include "LensDatabase.h"

#include <QHash>

QString LensDatabase::lensNameForId(uint64_t lensId) {
// Taken from exiftool/lib/Image/ExifTool/Nikon.pm
// Regexp to convert Perl to C++:
// '(\S\S) (\S\S) (\S\S) (\S\S) (\S\S) (\S\S) (\S\S) (\S\S)' => '(.*?)'
// {0x\1\2\3\4\5\6\7\8, "\9"}
    static QHash<uint64_t, QString> lensDB = {
    // Notes => q{
        // The Nikon LensID is constructed as a Composite tag from the raw hex values
        // of 8 other tags: LensIDNumber, LensFStops, MinFocalLength, MaxFocalLength,
        // MaxApertureAtMinFocal, MaxApertureAtMaxFocal, MCUVersion and LensType, in
        // that order.  The user-defined "Lenses" list may be used to specify the lens
        // for ExifTool to choose in these cases (see the
        // L<sample config file|../config.html> for details).
    // },
    // OTHER => \&LensIDConv,
    // Note: Sync this list with Robert's Perl version at
    // http://www.rottmerhusen.com/objektives/lensid/files/exif/fmountlens.p.txt
    // (hex digits must be uppercase in keys below)
    {0x0158505014140200, "AF Nikkor 50mm f/1.8"},
    {0x0158505014140500, "AF Nikkor 50mm f/1.8"},
    {0x0242445C2A340200, "AF Zoom-Nikkor 35-70mm f/3.3-4.5"},
    {0x0242445C2A340800, "AF Zoom-Nikkor 35-70mm f/3.3-4.5"},
    {0x03485C8130300200, "AF Zoom-Nikkor 70-210mm f/4"},
    {0x04483C3C24240300, "AF Nikkor 28mm f/2.8"},
    {0x055450500C0C0400, "AF Nikkor 50mm f/1.4"},
    {0x0654535324240600, "AF Micro-Nikkor 55mm f/2.8"},
    {0x07403C622C340300, "AF Zoom-Nikkor 28-85mm f/3.5-4.5"},
    {0x0840446A2C340400, "AF Zoom-Nikkor 35-105mm f/3.5-4.5"},
    {0x0948373724240400, "AF Nikkor 24mm f/2.8"},
    {0x0A488E8E24240300, "AF Nikkor 300mm f/2.8 IF-ED"},
    {0x0A488E8E24240500, "AF Nikkor 300mm f/2.8 IF-ED N"},
    {0x0B487C7C24240500, "AF Nikkor 180mm f/2.8 IF-ED"},
    {0x0D4044722C340700, "AF Zoom-Nikkor 35-135mm f/3.5-4.5"},
    {0x0E485C8130300500, "AF Zoom-Nikkor 70-210mm f/4"},
    {0x0F58505014140500, "AF Nikkor 50mm f/1.8 N"},
    {0x10488E8E30300800, "AF Nikkor 300mm f/4 IF-ED"},
    {0x1148445C24240800, "AF Zoom-Nikkor 35-70mm f/2.8"},
    {0x1148445C24241500, "AF Zoom-Nikkor 35-70mm f/2.8"}, //Jakob Dettner
    {0x12485C81303C0900, "AF Nikkor 70-210mm f/4-5.6"},
    {0x134237502A340B00, "AF Zoom-Nikkor 24-50mm f/3.3-4.5"},
    {0x1448608024240B00, "AF Zoom-Nikkor 80-200mm f/2.8 ED"},
    {0x154C626214140C00, "AF Nikkor 85mm f/1.8"},
    {0x173CA0A030300F00, "Nikkor 500mm f/4 P ED IF"},
    {0x173CA0A030301100, "Nikkor 500mm f/4 P ED IF"},
    {0x184044722C340E00, "AF Zoom-Nikkor 35-135mm f/3.5-4.5 N"},
    {0x1A54444418181100, "AF Nikkor 35mm f/2"},
    {0x1B445E8E343C1000, "AF Zoom-Nikkor 75-300mm f/4.5-5.6"},
    {0x1C48303024241200, "AF Nikkor 20mm f/2.8"},
    {0x1D42445C2A341200, "AF Zoom-Nikkor 35-70mm f/3.3-4.5 N"},
    {0x1E54565624241300, "AF Micro-Nikkor 60mm f/2.8"},
    {0x1F546A6A24241400, "AF Micro-Nikkor 105mm f/2.8"},
    {0x2048608024241500, "AF Zoom-Nikkor 80-200mm f/2.8 ED"},
    {0x21403C5C2C341600, "AF Zoom-Nikkor 28-70mm f/3.5-4.5"},
    {0x2248727218181600, "AF DC-Nikkor 135mm f/2"},
    {0x2330BECA3C481700, "Zoom-Nikkor 1200-1700mm f/5.6-8 P ED IF"},
    {0x2448608024241A02, "AF Zoom-Nikkor 80-200mm f/2.8D ED"},
    {0x2548445C24241B02, "AF Zoom-Nikkor 35-70mm f/2.8D"},
    {0x2548445C24243A02, "AF Zoom-Nikkor 35-70mm f/2.8D"},
    {0x2548445C24245202, "AF Zoom-Nikkor 35-70mm f/2.8D"},
    {0x26403C5C2C341C02, "AF Zoom-Nikkor 28-70mm f/3.5-4.5D"},
    {0x27488E8E24241D02, "AF-I Nikkor 300mm f/2.8D IF-ED"},
    {0x27488E8E2424F102, "AF-I Nikkor 300mm f/2.8D IF-ED + TC-14E"},
    {0x27488E8E2424E102, "AF-I Nikkor 300mm f/2.8D IF-ED + TC-17E"},
    {0x27488E8E2424F202, "AF-I Nikkor 300mm f/2.8D IF-ED + TC-20E"},
    {0x283CA6A630301D02, "AF-I Nikkor 600mm f/4D IF-ED"},
    {0x283CA6A63030F102, "AF-I Nikkor 600mm f/4D IF-ED + TC-14E"},
    {0x283CA6A63030E102, "AF-I Nikkor 600mm f/4D IF-ED + TC-17E"},
    {0x283CA6A63030F202, "AF-I Nikkor 600mm f/4D IF-ED + TC-20E"},
    {0x2A543C3C0C0C2602, "AF Nikkor 28mm f/1.4D"},
    {0x2B3C4460303C1F02, "AF Zoom-Nikkor 35-80mm f/4-5.6D"},
    {0x2C486A6A18182702, "AF DC-Nikkor 105mm f/2D"},
    {0x2D48808030302102, "AF Micro-Nikkor 200mm f/4D IF-ED"},
    {0x2E485C82303C2202, "AF Nikkor 70-210mm f/4-5.6D"},
    {0x2E485C82303C2802, "AF Nikkor 70-210mm f/4-5.6D"},
    {0x2F48304424242902, "AF Zoom-Nikkor 20-35mm f/2.8D IF"}, // there were two of them, one commented out
    {0x3048989824242402, "AF-I Nikkor 400mm f/2.8D IF-ED"},
    {0x304898982424F102, "AF-I Nikkor 400mm f/2.8D IF-ED + TC-14E"},
    {0x304898982424E102, "AF-I Nikkor 400mm f/2.8D IF-ED + TC-17E"},
    {0x304898982424F202, "AF-I Nikkor 400mm f/2.8D IF-ED + TC-20E"},
    {0x3154565624242502, "AF Micro-Nikkor 60mm f/2.8D"},
    {0x32546A6A24243502, "AF Micro-Nikkor 105mm f/2.8D"}, // there were two of them, one commented out
    {0x33482D2D24243102, "AF Nikkor 18mm f/2.8D"},
    {0x3448292924243202, "AF Fisheye Nikkor 16mm f/2.8D"},
    {0x353CA0A030303302, "AF-I Nikkor 500mm f/4D IF-ED"},
    {0x353CA0A03030F102, "AF-I Nikkor 500mm f/4D IF-ED + TC-14E"},
    {0x353CA0A03030E102, "AF-I Nikkor 500mm f/4D IF-ED + TC-17E"},
    {0x353CA0A03030F202, "AF-I Nikkor 500mm f/4D IF-ED + TC-20E"},
    {0x3648373724243402, "AF Nikkor 24mm f/2.8D"},
    {0x3748303024243602, "AF Nikkor 20mm f/2.8D"},
    {0x384C626214143702, "AF Nikkor 85mm f/1.8D"},
    {0x3A403C5C2C343902, "AF Zoom-Nikkor 28-70mm f/3.5-4.5D"},
    {0x3B48445C24243A02, "AF Zoom-Nikkor 35-70mm f/2.8D N"},
    {0x3C48608024243B02, "AF Zoom-Nikkor 80-200mm f/2.8D ED"}, //NJ
    {0x3D3C4460303C3E02, "AF Zoom-Nikkor 35-80mm f/4-5.6D"},
    {0x3E483C3C24243D02, "AF Nikkor 28mm f/2.8D"},
    {0x3F40446A2C344502, "AF Zoom-Nikkor 35-105mm f/3.5-4.5D"},
    {0x41487C7C24244302, "AF Nikkor 180mm f/2.8D IF-ED"},
    {0x4254444418184402, "AF Nikkor 35mm f/2D"},
    {0x435450500C0C4602, "AF Nikkor 50mm f/1.4D"},
    {0x44446080343C4702, "AF Zoom-Nikkor 80-200mm f/4.5-5.6D"},
    {0x45403C602C3C4802, "AF Zoom-Nikkor 28-80mm f/3.5-5.6D"},
    {0x463C4460303C4902, "AF Zoom-Nikkor 35-80mm f/4-5.6D N"},
    {0x474237502A344A02, "AF Zoom-Nikkor 24-50mm f/3.3-4.5D"},
    {0x48488E8E24244B02, "AF-S Nikkor 300mm f/2.8D IF-ED"},
    {0x48488E8E2424F102, "AF-S Nikkor 300mm f/2.8D IF-ED + TC-14E"},
    {0x48488E8E2424E102, "AF-S Nikkor 300mm f/2.8D IF-ED + TC-17E"},
    {0x48488E8E2424F202, "AF-S Nikkor 300mm f/2.8D IF-ED + TC-20E"},
    {0x493CA6A630304C02, "AF-S Nikkor 600mm f/4D IF-ED"},
    {0x493CA6A63030F102, "AF-S Nikkor 600mm f/4D IF-ED + TC-14E"},
    {0x493CA6A63030E102, "AF-S Nikkor 600mm f/4D IF-ED + TC-17E"},
    {0x493CA6A63030F202, "AF-S Nikkor 600mm f/4D IF-ED + TC-20E"},
    {0x4A5462620C0C4D02, "AF Nikkor 85mm f/1.4D IF"},
    {0x4B3CA0A030304E02, "AF-S Nikkor 500mm f/4D IF-ED"},
    {0x4B3CA0A03030F102, "AF-S Nikkor 500mm f/4D IF-ED + TC-14E"},
    {0x4B3CA0A03030E102, "AF-S Nikkor 500mm f/4D IF-ED + TC-17E"},
    {0x4B3CA0A03030F202, "AF-S Nikkor 500mm f/4D IF-ED + TC-20E"},
    {0x4C40376E2C3C4F02, "AF Zoom-Nikkor 24-120mm f/3.5-5.6D IF"},
    {0x4D403C802C3C6202, "AF Zoom-Nikkor 28-200mm f/3.5-5.6D IF"},
    {0x4E48727218185102, "AF DC-Nikkor 135mm f/2D"},
    {0x4F40375C2C3C5306, "IX-Nikkor 24-70mm f/3.5-5.6"},
    {0x5048567C303C5406, "IX-Nikkor 60-180mm f/4-5.6"},
    {0x5348608024245702, "AF Zoom-Nikkor 80-200mm f/2.8D ED"},
    {0x5348608024246002, "AF Zoom-Nikkor 80-200mm f/2.8D ED"},
    {0x54445C7C343C5802, "AF Zoom-Micro Nikkor 70-180mm f/4.5-5.6D ED"},
    {0x54445C7C343C6102, "AF Zoom-Micro Nikkor 70-180mm f/4.5-5.6D ED"},
    {0x56485C8E303C5A02, "AF Zoom-Nikkor 70-300mm f/4-5.6D ED"},
    {0x5948989824245D02, "AF-S Nikkor 400mm f/2.8D IF-ED"},
    {0x594898982424F102, "AF-S Nikkor 400mm f/2.8D IF-ED + TC-14E"},
    {0x594898982424E102, "AF-S Nikkor 400mm f/2.8D IF-ED + TC-17E"},
    {0x594898982424F202, "AF-S Nikkor 400mm f/2.8D IF-ED + TC-20E"},
    {0x5A3C3E56303C5E06, "IX-Nikkor 30-60mm f/4-5.6"},
    {0x5B44567C343C5F06, "IX-Nikkor 60-180mm f/4.5-5.6"},
    {0x5D483C5C24246302, "AF-S Zoom-Nikkor 28-70mm f/2.8D IF-ED"},
    {0x5E48608024246402, "AF-S Zoom-Nikkor 80-200mm f/2.8D IF-ED"},
    {0x5F403C6A2C346502, "AF Zoom-Nikkor 28-105mm f/3.5-4.5D IF"},
    {0x60403C602C3C6602, "AF Zoom-Nikkor 28-80mm f/3.5-5.6D"}, //(http://www.exif.org/forum/topic.asp?TOPIC_ID=16)
    {0x61445E86343C6702, "AF Zoom-Nikkor 75-240mm f/4.5-5.6D"},
    {0x63482B4424246802, "AF-S Nikkor 17-35mm f/2.8D IF-ED"},
    {0x6400626224246A02, "PC Micro-Nikkor 85mm f/2.8D"},
    {0x65446098343C6B0A, "AF VR Zoom-Nikkor 80-400mm f/4.5-5.6D ED"},
    {0x66402D442C346C02, "AF Zoom-Nikkor 18-35mm f/3.5-4.5D IF-ED"},
    {0x6748376224306D02, "AF Zoom-Nikkor 24-85mm f/2.8-4D IF"},
    {0x68423C602A3C6E06, "AF Zoom-Nikkor 28-80mm f/3.3-5.6G"},
    {0x69485C8E303C6F06, "AF Zoom-Nikkor 70-300mm f/4-5.6G"},
    {0x6A488E8E30307002, "AF-S Nikkor 300mm f/4D IF-ED"},
    {0x6B48242424247102, "AF Nikkor ED 14mm f/2.8D"},
    {0x6D488E8E24247302, "AF-S Nikkor 300mm f/2.8D IF-ED II"},
    {0x6E48989824247402, "AF-S Nikkor 400mm f/2.8D IF-ED II"},
    {0x6F3CA0A030307502, "AF-S Nikkor 500mm f/4D IF-ED II"},
    {0x703CA6A630307602, "AF-S Nikkor 600mm f/4D IF-ED II"},
    {0x72484C4C24247700, "Nikkor 45mm f/2.8 P"},
    {0x744037622C347806, "AF-S Zoom-Nikkor 24-85mm f/3.5-4.5G IF-ED"},
    {0x75403C682C3C7906, "AF Zoom-Nikkor 28-100mm f/3.5-5.6G"},
    {0x7658505014147A02, "AF Nikkor 50mm f/1.8D"},
    {0x77485C8024247B0E, "AF-S VR Zoom-Nikkor 70-200mm f/2.8G IF-ED"},
    {0x7840376E2C3C7C0E, "AF-S VR Zoom-Nikkor 24-120mm f/3.5-5.6G IF-ED"},
    {0x79403C802C3C7F06, "AF Zoom-Nikkor 28-200mm f/3.5-5.6G IF-ED"},
    {0x7A3C1F3730307E06, "AF-S DX Zoom-Nikkor 12-24mm f/4G IF-ED"}, // there were two of them, one commented out
    {0x7B4880983030800E, "AF-S VR Zoom-Nikkor 200-400mm f/4G IF-ED"},
    {0x7D482B5324248206, "AF-S DX Zoom-Nikkor 17-55mm f/2.8G IF-ED"},
    {0x7F402D5C2C348406, "AF-S DX Zoom-Nikkor 18-70mm f/3.5-4.5G IF-ED"},
    {0x80481A1A24248506, "AF DX Fisheye-Nikkor 10.5mm f/2.8G ED"},
    {0x815480801818860E, "AF-S VR Nikkor 200mm f/2G IF-ED"},
    {0x82488E8E2424870E, "AF-S VR Nikkor 300mm f/2.8G IF-ED"},
    {0x8300B0B05A5A8804, "FSA-L2, EDG 65, 800mm F13 G"},
    {0x893C5380303C8B06, "AF-S DX Zoom-Nikkor 55-200mm f/4-5.6G ED"},
    {0x8A546A6A24248C0E, "AF-S VR Micro-Nikkor 105mm f/2.8G IF-ED"}, //10
    // when the TC-20 III 2x teleconverter is used with the above lens, the following have been observed:
    // 8A 4D 6A 6A 24 24 8C 0E
    // 8A 4E 6A 6A 24 24 8C 0E
    // 8A 50 6A 6A 24 24 8C 0E
    // 8A 51 6A 6A 24 24 8C 0E
    // 8A 52 6A 6A 24 24 8C 0E
    // 8A 53 6A 6A 24 24 8C 0E
    // 8A 54 6A 6A 24 24 8C 0E (same as without the TC)
    {0x8B402D802C3C8D0E, "AF-S DX VR Zoom-Nikkor 18-200mm f/3.5-5.6G IF-ED"},
    {0x8B402D802C3CFD0E, "AF-S DX VR Zoom-Nikkor 18-200mm f/3.5-5.6G IF-ED [II]"}, //20
    {0x8C402D532C3C8E06, "AF-S DX Zoom-Nikkor 18-55mm f/3.5-5.6G ED"},
    {0x8D445C8E343C8F0E, "AF-S VR Zoom-Nikkor 70-300mm f/4.5-5.6G IF-ED"}, //10
    {0x8F402D722C3C9106, "AF-S DX Zoom-Nikkor 18-135mm f/3.5-5.6G IF-ED"},
    {0x903B5380303C920E, "AF-S DX VR Zoom-Nikkor 55-200mm f/4-5.6G IF-ED"},
    {0x9248243724249406, "AF-S Zoom-Nikkor 14-24mm f/2.8G ED"},
    {0x9348375C24249506, "AF-S Zoom-Nikkor 24-70mm f/2.8G ED"},
    {0x94402D532C3C9606, "AF-S DX Zoom-Nikkor 18-55mm f/3.5-5.6G ED II"}, //10 (D40)
    {0x954C37372C2C9702, "PC-E Nikkor 24mm f/3.5D ED"},
    {0x950037372C2C9706, "PC-E Nikkor 24mm f/3.5D ED"}, //JD
    {0x964898982424980E, "AF-S VR Nikkor 400mm f/2.8G ED"},
    {0x973CA0A03030990E, "AF-S VR Nikkor 500mm f/4G ED"},
    {0x983CA6A630309A0E, "AF-S VR Nikkor 600mm f/4G ED"},
    {0x994029622C3C9B0E, "AF-S DX VR Zoom-Nikkor 16-85mm f/3.5-5.6G ED"},
    {0x9A402D532C3C9C0E, "AF-S DX VR Zoom-Nikkor 18-55mm f/3.5-5.6G"},
    {0x9B544C4C24249D02, "PC-E Micro Nikkor 45mm f/2.8D ED"},
    {0x9B004C4C24249D06, "PC-E Micro Nikkor 45mm f/2.8D ED"},
    {0x9C54565624249E06, "AF-S Micro Nikkor 60mm f/2.8G ED"},
    {0x9D54626224249F02, "PC-E Micro Nikkor 85mm f/2.8D"},
    {0x9D00626224249F06, "PC-E Micro Nikkor 85mm f/2.8D"},
    {0x9E402D6A2C3CA00E, "AF-S DX VR Zoom-Nikkor 18-105mm f/3.5-5.6G ED"}, //PH/10
    {0x9F5844441414A106, "AF-S DX Nikkor 35mm f/1.8G"}, //27
    {0xA05450500C0CA206, "AF-S Nikkor 50mm f/1.4G"},
    {0xA14018372C34A306, "AF-S DX Nikkor 10-24mm f/3.5-4.5G ED"},
    {0xA1402D532C3CCB86, "AF-P DX Nikkor 18-55mm f/3.5-5.6G"}, //30
    {0xA2485C802424A40E, "AF-S Nikkor 70-200mm f/2.8G ED VR II"},
    {0xA33C29443030A50E, "AF-S Nikkor 16-35mm f/4G ED VR"},
    {0xA45437370C0CA606, "AF-S Nikkor 24mm f/1.4G ED"},
    {0xA5403C8E2C3CA70E, "AF-S Nikkor 28-300mm f/3.5-5.6G ED VR"},
    {0xA6488E8E2424A80E, "AF-S Nikkor 300mm f/2.8G IF-ED VR II"},
    {0xA74B62622C2CA90E, "AF-S DX Micro Nikkor 85mm f/3.5G ED VR"},
    {0xA84880983030AA0E, "AF-S Zoom-Nikkor 200-400mm f/4G IF-ED VR II"}, //https://exiftool.org/forum/index.php/topic,3218.msg15495.html//msg15495
    {0xA95480801818AB0E, "AF-S Nikkor 200mm f/2G ED VR II"},
    {0xAA3C376E3030AC0E, "AF-S Nikkor 24-120mm f/4G ED VR"},
    {0xAC38538E343CAE0E, "AF-S DX Nikkor 55-300mm f/4.5-5.6G ED VR"},
    {0xAD3C2D8E2C3CAF0E, "AF-S DX Nikkor 18-300mm f/3.5-5.6G ED VR"},
    {0xAE5462620C0CB006, "AF-S Nikkor 85mm f/1.4G"},
    {0xAF5444440C0CB106, "AF-S Nikkor 35mm f/1.4G"},
    {0xB04C50501414B206, "AF-S Nikkor 50mm f/1.8G"},
    {0xB14848482424B306, "AF-S DX Micro Nikkor 40mm f/2.8G"}, //27
    {0xB2485C803030B40E, "AF-S Nikkor 70-200mm f/4G ED VR"}, //35
    {0xB34C62621414B506, "AF-S Nikkor 85mm f/1.8G"},
    {0xB44037622C34B60E, "AF-S Zoom-Nikkor 24-85mm f/3.5-4.5G IF-ED VR"}, //30
    {0xB54C3C3C1414B706, "AF-S Nikkor 28mm f/1.8G"}, //30
    {0xB63CB0B03C3CB80E, "AF-S VR Nikkor 800mm f/5.6E FL ED"},
    {0xB63CB0B03C3CB84E, "AF-S VR Nikkor 800mm f/5.6E FL ED"}, //PH
    {0xB7446098343CB90E, "AF-S Nikkor 80-400mm f/4.5-5.6G ED VR"},
    {0xB8402D442C34BA06, "AF-S Nikkor 18-35mm f/3.5-4.5G ED"},
    {0xA0402D742C3CBB0E, "AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR"}, //PH
    {0xA15455550C0CBC06, "AF-S Nikkor 58mm f/1.4G"}, //IB
    {0xA1486E8E2424DB4E, "AF-S Nikkor 120-300mm f/2.8E FL ED SR VR"}, //28
    {0xA2402D532C3CBD0E, "AF-S DX Nikkor 18-55mm f/3.5-5.6G VR II"},
    {0xA4402D8E2C40BF0E, "AF-S DX Nikkor 18-300mm f/3.5-6.3G ED VR"},
    {0xA54C44441414C006, "AF-S Nikkor 35mm f/1.8G ED"}, //35 ("ED" ref 11)
    {0xA64898982424C10E, "AF-S Nikkor 400mm f/2.8E FL ED VR"},
    {0xA73C5380303CC20E, "AF-S DX Nikkor 55-200mm f/4-5.6G ED VR II"}, //IB
    {0xA8488E8E3030C34E, "AF-S Nikkor 300mm f/4E PF ED VR"}, //35
    {0xA8488E8E3030C30E, "AF-S Nikkor 300mm f/4E PF ED VR"}, //30
    {0xA94C31311414C406, "AF-S Nikkor 20mm f/1.8G ED"}, //30
    {0xAA48375C2424C54E, "AF-S Nikkor 24-70mm f/2.8E ED VR"},
    {0xAA48375C2424C50E, "AF-S Nikkor 24-70mm f/2.8E ED VR"},
    {0xAB3CA0A03030C64E, "AF-S Nikkor 500mm f/4E FL ED VR"},
    {0xAC3CA6A63030C74E, "AF-S Nikkor 600mm f/4E FL ED VR"}, //PH
    {0xAD4828602430C84E, "AF-S DX Nikkor 16-80mm f/2.8-4E ED VR"},
    {0xAD4828602430C80E, "AF-S DX Nikkor 16-80mm f/2.8-4E ED VR"}, //PH
    {0xAE3C80A03C3CC94E, "AF-S Nikkor 200-500mm f/5.6E ED VR"}, //PH
    {0xAE3C80A03C3CC90E, "AF-S Nikkor 200-500mm f/5.6E ED VR"},
    {0xA0402D532C3CCA8E, "AF-P DX Nikkor 18-55mm f/3.5-5.6G"}, //Yang You pvt communication
    {0xA0402D532C3CCA0E, "AF-P DX Nikkor 18-55mm f/3.5-5.6G VR"}, //PH
    {0xAF4C37371414CC06, "AF-S Nikkor 24mm f/1.8G ED"}, //IB
    {0xA2385C8E3440CD86, "AF-P DX Nikkor 70-300mm f/4.5-6.3G VR"}, //PH
    {0xA3385C8E3440CE8E, "AF-P DX Nikkor 70-300mm f/4.5-6.3G ED VR"},
    {0xA3385C8E3440CE0E, "AF-P DX Nikkor 70-300mm f/4.5-6.3G ED"},
    {0xA4485C802424CF4E, "AF-S Nikkor 70-200mm f/2.8E FL ED VR"},
    {0xA4485C802424CF0E, "AF-S Nikkor 70-200mm f/2.8E FL ED VR"},
    {0xA5546A6A0C0CD046, "AF-S Nikkor 105mm f/1.4E ED"}, //IB
    {0xA5546A6A0C0CD006, "AF-S Nikkor 105mm f/1.4E ED"}, //IB
    {0xA6482F2F3030D146, "PC Nikkor 19mm f/4E ED"},
    {0xA6482F2F3030D106, "PC Nikkor 19mm f/4E ED"},
    {0xA74011262C34D246, "AF-S Fisheye Nikkor 8-15mm f/3.5-4.5E ED"},
    {0xA74011262C34D206, "AF-S Fisheye Nikkor 8-15mm f/3.5-4.5E ED"},
    {0xA8381830343CD38E, "AF-P DX Nikkor 10-20mm f/4.5-5.6G VR"}, //Yang You pvt communication
    {0xA8381830343CD30E, "AF-P DX Nikkor 10-20mm f/4.5-5.6G VR"},
    {0xA9487C983030D44E, "AF-S Nikkor 180-400mm f/4E TC1.4 FL ED VR"}, //IB
    {0xA9487C983030D40E, "AF-S Nikkor 180-400mm f/4E TC1.4 FL ED VR"},
    {0xAA4888A43C3CD54E, "AF-S Nikkor 180-400mm f/4E TC1.4 FL ED VR + 1.4x TC"}, //IB
    {0xAA4888A43C3CD50E, "AF-S Nikkor 180-400mm f/4E TC1.4 FL ED VR + 1.4x TC"},
    {0xAB445C8E343CD6CE, "AF-P Nikkor 70-300mm f/4.5-5.6E ED VR"},
    {0xAB445C8E343CD60E, "AF-P Nikkor 70-300mm f/4.5-5.6E ED VR"},
    {0xAB445C8E343CD64E, "AF-P Nikkor 70-300mm f/4.5-5.6E ED VR"}, //IB
    {0xAC543C3C0C0CD746, "AF-S Nikkor 28mm f/1.4E ED"},
    {0xAC543C3C0C0CD706, "AF-S Nikkor 28mm f/1.4E ED"},
    {0xAD3CA0A03C3CD80E, "AF-S Nikkor 500mm f/5.6E PF ED VR"},
    {0xAD3CA0A03C3CD84E, "AF-S Nikkor 500mm f/5.6E PF ED VR"},
    {0x0100000000000200, "TC-16A"},
    {0x0100000000000800, "TC-16A"},
    {0x000000000000F10C, "TC-14E [II] or Sigma APO Tele Converter 1.4x EX DG or Kenko Teleplus PRO 300 DG 1.4x"},
    {0x000000000000F218, "TC-20E [II] or Sigma APO Tele Converter 2x EX DG or Kenko Teleplus PRO 300 DG 2.0x"},
    {0x000000000000E112, "TC-17E II"},
    {0xFE47000024244B06, "Sigma 4.5mm F2.8 EX DC HSM Circular Fisheye"}, //JD
    {0x2648111130301C02, "Sigma 8mm F4 EX Circular Fisheye"},
    {0x794011112C2C1C06, "Sigma 8mm F3.5 EX Circular Fisheye"}, //JD
    {0xDB4011112C2C1C06, "Sigma 8mm F3.5 EX DG Circular Fisheye"}, //30
    {0xDC48191924244B06, "Sigma 10mm F2.8 EX DC HSM Fisheye"},
    {0xC24C242414144B06, "Sigma 14mm F1.8 DG HSM | A"}, //IB
    {0x4848242424244B02, "Sigma 14mm F2.8 EX Aspherical HSM"},
    {0x023F24242C2C0200, "Sigma 14mm F3.5"},
    {0x2648272724241C02, "Sigma 15mm F2.8 EX Diagonal Fisheye"},
    {0xEA48272724241C02, "Sigma 15mm F2.8 EX Diagonal Fisheye"}, //30
    {0x2658313114141C02, "Sigma 20mm F1.8 EX DG Aspherical RF"},
    {0x795431310C0C4B06, "Sigma 20mm F1.4 DG HSM | A"}, //Rolf Probst
    {0x2658373714141C02, "Sigma 24mm F1.8 EX DG Aspherical Macro"},
    {0xE158373714141C02, "Sigma 24mm F1.8 EX DG Aspherical Macro"},
    {0x0246373725250200, "Sigma 24mm F2.8 Super Wide II Macro"},
    {0x7E5437370C0C4B06, "Sigma 24mm F1.4 DG HSM | A"}, //30
    {0x26583C3C14141C02, "Sigma 28mm F1.8 EX DG Aspherical Macro"},
    {0xBC543C3C0C0C4B46, "Sigma 28mm F1.4 DG HSM | A"}, //30
    {0x48543E3E0C0C4B06, "Sigma 30mm F1.4 EX DC HSM"},
    {0xF8543E3E0C0C4B06, "Sigma 30mm F1.4 EX DC HSM"}, //JD
    {0x915444440C0C4B06, "Sigma 35mm F1.4 DG HSM"}, //30
    {0xBD5448480C0C4B46, "Sigma 40mm F1.4 DG HSM | A"}, //30
    {0xDE5450500C0C4B06, "Sigma 50mm F1.4 EX DG HSM"},
    {0x885450500C0C4B06, "Sigma 50mm F1.4 DG HSM | A"},
    {0x0248505024240200, "Sigma Macro 50mm F2.8"}, //https://exiftool.org/forum/index.php/topic,4027.0.html
    {0x3254505024243502, "Sigma Macro 50mm F2.8 EX DG"},
    {0xE354505024243502, "Sigma Macro 50mm F2.8 EX DG"}, //https://exiftool.org/forum/index.php/topic,3215.0.html
    {0x79485C5C24241C06, "Sigma Macro 70mm F2.8 EX DG"}, //JD
    {0x9B5462620C0C4B06, "Sigma 85mm F1.4 EX DG HSM"},
    {0xC85462620C0C4B46, "Sigma 85mm F1.4 DG HSM | A"}, //JamiBradley
    {0xC85462620C0C4B06, "Sigma 85mm F1.4 DG HSM | A"}, //KennethCochran
    {0x0248656524240200, "Sigma Macro 90mm F2.8"},
    // '32 54 6A 6A 24 24 35 02.2' => 'Sigma Macro 105mm F2.8 EX DG', //JD
    {0xE5546A6A24243502, "Sigma Macro 105mm F2.8 EX DG"},
    {0x97486A6A24244B0E, "Sigma Macro 105mm F2.8 EX DG OS HSM"},
    {0xBE546A6A0C0C4B46, "Sigma 105mm F1.4 DG HSM | A"}, //30
    {0x4848767624244B06, "Sigma APO Macro 150mm F2.8 EX DG HSM"},
    {0xF548767624244B06, "Sigma APO Macro 150mm F2.8 EX DG HSM"}, //24
    {0x9948767624244B0E, "Sigma APO Macro 150mm F2.8 EX DG OS HSM"}, //(Christian Hesse)
    {0x484C7C7C2C2C4B02, "Sigma APO Macro 180mm F3.5 EX DG HSM"},
    {0x484C7D7D2C2C4B02, "Sigma APO Macro 180mm F3.5 EX DG HSM"},
    {0xF44C7C7C2C2C4B02, "Sigma APO Macro 180mm F3.5 EX DG HSM"}, //Bruno
    {0x94487C7C24244B0E, "Sigma APO Macro 180mm F2.8 EX DG OS HSM"}, //MichaelTapes (HSM from ref 8)
    {0x48548E8E24244B02, "Sigma APO 300mm F2.8 EX DG HSM"},
    {0xFB548E8E24244B02, "Sigma APO 300mm F2.8 EX DG HSM"}, //26
    {0x26488E8E30301C02, "Sigma APO Tele Macro 300mm F4"},
    {0x022F98983D3D0200, "Sigma APO 400mm F5.6"},
    {0x263C98983C3C1C02, "Sigma APO Tele Macro 400mm F5.6"},
    {0x0237A0A034340200, "Sigma APO 500mm F4.5"}, //19
    {0x4844A0A034344B02, "Sigma APO 500mm F4.5 EX HSM"},
    {0xF144A0A034344B02, "Sigma APO 500mm F4.5 EX DG HSM"},
    {0x0234A0A044440200, "Sigma APO 500mm F7.2"},
    {0x023CB0B03C3C0200, "Sigma APO 800mm F5.6"},
    {0x483CB0B03C3C4B02, "Sigma APO 800mm F5.6 EX HSM"},
    {0x9E381129343C4B06, "Sigma 8-16mm F4.5-5.6 DC HSM"},
    {0xA14119312C2C4B06, "Sigma 10-20mm F3.5 EX DC HSM"},
    {0x483C1931303C4B06, "Sigma 10-20mm F4-5.6 EX DC HSM"},
    {0xF93C1931303C4B06, "Sigma 10-20mm F4-5.6 EX DC HSM"}, //JD
    {0x48381F37343C4B06, "Sigma 12-24mm F4.5-5.6 EX DG Aspherical HSM"},
    {0xF0381F37343C4B06, "Sigma 12-24mm F4.5-5.6 EX DG Aspherical HSM"},
    {0x96381F37343C4B06, "Sigma 12-24mm F4.5-5.6 II DG HSM"}, //Jurgen Sahlberg
    {0xCA3C1F3730304B46, "Sigma 12-24mm F4 DG HSM | A"}, //github issue//101
    {0xC148243724244B46, "Sigma 14-24mm F2.8 DG HSM | A"}, //30
    {0x2640273F2C341C02, "Sigma 15-30mm F3.5-4.5 EX DG Aspherical DF"},
    {0x48482B4424304B06, "Sigma 17-35mm F2.8-4 EX DG  Aspherical HSM"},
    {0x26542B4424301C02, "Sigma 17-35mm F2.8-4 EX Aspherical"},
    {0x9D482B5024244B0E, "Sigma 17-50mm F2.8 EX DC OS HSM"},
    {0x8F482B5024244B0E, "Sigma 17-50mm F2.8 EX DC OS HSM"}, //http://dev.exiv2.org/boards/3/topics/1747
    {0x7A472B5C24344B06, "Sigma 17-70mm F2.8-4.5 DC Macro Asp. IF HSM"},
    {0x7A482B5C24344B06, "Sigma 17-70mm F2.8-4.5 DC Macro Asp. IF HSM"},
    {0x7F482B5C24341C06, "Sigma 17-70mm F2.8-4.5 DC Macro Asp. IF"},
    {0x8E3C2B5C24304B0E, "Sigma 17-70mm F2.8-4 DC Macro OS HSM | C"},
    {0xA0482A5C24304B0E, "Sigma 17-70mm F2.8-4 DC Macro OS HSM"}, //https://exiftool.org/forum/index.php/topic,5170.0.html
    {0x8B4C2D4414144B06, "Sigma 18-35mm F1.8 DC HSM"}, //30/NJ
    {0x26402D442B341C02, "Sigma 18-35mm F3.5-4.5 Aspherical"},
    {0x26482D5024241C06, "Sigma 18-50mm F2.8 EX DC"},
    {0x7F482D5024241C06, "Sigma 18-50mm F2.8 EX DC Macro"}, //NJ
    {0x7A482D5024244B06, "Sigma 18-50mm F2.8 EX DC Macro"},
    {0xF6482D5024244B06, "Sigma 18-50mm F2.8 EX DC Macro"},
    {0xA4472D5024344B0E, "Sigma 18-50mm F2.8-4.5 DC OS HSM"},
    {0x26402D502C3C1C06, "Sigma 18-50mm F3.5-5.6 DC"},
    {0x7A402D502C3C4B06, "Sigma 18-50mm F3.5-5.6 DC HSM"},
    {0x26402D702B3C1C06, "Sigma 18-125mm F3.5-5.6 DC"},
    {0xCD3D2D702E3C4B0E, "Sigma 18-125mm F3.8-5.6 DC OS HSM"},
    {0x26402D802C401C06, "Sigma 18-200mm F3.5-6.3 DC"},
    {0xFF402D802C404B06, "Sigma 18-200mm F3.5-6.3 DC"}, //30
    {0x7A402D802C404B0E, "Sigma 18-200mm F3.5-6.3 DC OS HSM"},
    {0xED402D802C404B0E, "Sigma 18-200mm F3.5-6.3 DC OS HSM"}, //JD
    {0x90402D802C404B0E, "Sigma 18-200mm F3.5-6.3 II DC OS HSM"}, //JohnHelour
    {0x89302D802C404B0E, "Sigma 18-200mm F3.5-6.3 DC Macro OS HS | C"}, //JoeSchonberg
    {0xA5402D882C404B0E, "Sigma 18-250mm F3.5-6.3 DC OS HSM"},
    //  LensFStops varies with FocalLength for this lens (ref 2):
    {0x922C2D882C404B0E, "Sigma 18-250mm F3.5-6.3 DC Macro OS HSM"}, //2
    {0x872C2D8E2C404B0E, "Sigma 18-300mm F3.5-6.3 DC Macro HSM"}, //30
  // '92 2C 2D 88 2C 40 4B 0E' (250mm)
  // '92 2B 2D 88 2C 40 4B 0E' (210mm)
  // '92 2C 2D 88 2C 40 4B 0E' (185mm)
  // '92 2D 2D 88 2C 40 4B 0E' (155mm)
  // '92 2E 2D 88 2C 40 4B 0E' (130mm)
  // '92 2F 2D 88 2C 40 4B 0E' (105mm)
  // '92 30 2D 88 2C 40 4B 0E' (90mm)
  // '92 32 2D 88 2C 40 4B 0E' (75mm)
  // '92 33 2D 88 2C 40 4B 0E' (62mm)
  // '92 35 2D 88 2C 40 4B 0E' (52mm)
  // '92 37 2D 88 2C 40 4B 0E' (44mm)
  // '92 39 2D 88 2C 40 4B 0E' (38mm)
  // '92 3A 2D 88 2C 40 4B 0E' (32mm)
  // '92 3E 2D 88 2C 40 4B 0E' (22mm)
  // '92 40 2D 88 2C 40 4B 0E' (18mm)
    {0x2648314924241C02, "Sigma 20-40mm F2.8"},
    {0x7B48374418184B06, "Sigma 24-35mm F2.0 DG HSM | A"}, //30
    {0x023A3750313D0200, "Sigma 24-50mm F4-5.6 UC"},
    {0x2648375624241C02, "Sigma 24-60mm F2.8 EX DG"},
    {0xB648375624241C02, "Sigma 24-60mm F2.8 EX DG"},
    {0xA648375C24244B06, "Sigma 24-70mm F2.8 IF EX DG HSM"}, //JD
    {0xC948375C24244B4E, "Sigma 24-70mm F2.8 DG OS HSM | A"}, //30
    {0x2654375C24241C02, "Sigma 24-70mm F2.8 EX DG Macro"},
    {0x6754375C24241C02, "Sigma 24-70mm F2.8 EX DG Macro"},
    {0xE954375C24241C02, "Sigma 24-70mm F2.8 EX DG Macro"},
    {0x2640375C2C3C1C02, "Sigma 24-70mm F3.5-5.6 Aspherical HF"},
    {0x8A3C376A30304B0E, "Sigma 24-105mm F4 DG OS HSM"}, //IB
    {0x2654377324341C02, "Sigma 24-135mm F2.8-4.5"},
    {0x02463C5C25250200, "Sigma 28-70mm F2.8"},
    {0x26543C5C24241C02, "Sigma 28-70mm F2.8 EX"},
    {0x26483C5C24241C06, "Sigma 28-70mm F2.8 EX DG"},
    {0x79483C5C24241C06, "Sigma 28-70mm F2.8 EX DG"}, //30 ("D" removed)
    {0x26483C5C24301C02, "Sigma 28-70mm F2.8-4 DG"},
    {0x023F3C5C2D350200, "Sigma 28-70mm F3.5-4.5 UC"},
    {0x26403C602C3C1C02, "Sigma 28-80mm F3.5-5.6 Mini Zoom Macro II Aspherical"},
    {0x26403C652C3C1C02, "Sigma 28-90mm F3.5-5.6 Macro"},
    {0x26483C6A24301C02, "Sigma 28-105mm F2.8-4 Aspherical"},
    {0x263E3C6A2E3C1C02, "Sigma 28-105mm F3.8-5.6 UC-III Aspherical IF"},
    {0x26403C802C3C1C02, "Sigma 28-200mm F3.5-5.6 Compact Aspherical Hyperzoom Macro"},
    {0x26403C802B3C1C02, "Sigma 28-200mm F3.5-5.6 Compact Aspherical Hyperzoom Macro"},
    {0x263D3C802F3D1C02, "Sigma 28-300mm F3.8-5.6 Aspherical"},
    {0x26413C8E2C401C02, "Sigma 28-300mm F3.5-6.3 DG Macro"},
    {0xE6413C8E2C401C02, "Sigma 28-300mm F3.5-6.3 DG Macro"}, //https://exiftool.org/forum/index.php/topic,3301.0.html
    {0x26403C8E2C401C02, "Sigma 28-300mm F3.5-6.3 Macro"},
    {0x023B4461303D0200, "Sigma 35-80mm F4-5.6"},
    {0x024044732B360200, "Sigma 35-135mm F3.5-4.5 a"},
    {0xCC4C506814144B06, "Sigma 50-100mm F1.8 DC HSM | A"}, //30
    {0x7A47507624244B06, "Sigma 50-150mm F2.8 EX APO DC HSM"},
    {0xFD47507624244B06, "Sigma 50-150mm F2.8 EX APO DC HSM II"},
    {0x9848507624244B0E, "Sigma 50-150mm F2.8 EX APO DC OS HSM"}, //30
    {0x483C50A030404B02, "Sigma 50-500mm F4-6.3 EX APO RF HSM"},
    {0x9F3750A034404B0E, "Sigma 50-500mm F4.5-6.3 DG OS HSM"}, //16
    {0x263C5480303C1C06, "Sigma 55-200mm F4-5.6 DC"},
    {0x7A3B5380303C4B06, "Sigma 55-200mm F4-5.6 DC HSM"},
    {0x48545C8024244B02, "Sigma 70-200mm F2.8 EX APO IF HSM"},
    {0x7A485C8024244B06, "Sigma 70-200mm F2.8 EX APO DG Macro HSM II"},
    {0xEE485C8024244B06, "Sigma 70-200mm F2.8 EX APO DG Macro HSM II"}, //JD
    {0x9C485C8024244B0E, "Sigma 70-200mm F2.8 EX DG OS HSM"}, //Rolando Ruzic
    {0xBB485C8024244B4E, "Sigma 70-200mm F2.8 DG OS HSM | S"}, //forum13207
    {0x02465C8225250200, "Sigma 70-210mm F2.8 APO"}, //JD
    {0x02405C822C350200, "Sigma APO 70-210mm F3.5-4.5"},
    {0x263C5C82303C1C02, "Sigma 70-210mm F4-5.6 UC-II"},
    {0x023B5C82303C0200, "Sigma Zoom-K 70-210mm F4-5.6"}, //30
    {0x263C5C8E303C1C02, "Sigma 70-300mm F4-5.6 DG Macro"},
    {0x563C5C8E303C1C02, "Sigma 70-300mm F4-5.6 APO Macro Super II"},
    {0xE03C5C8E303C4B06, "Sigma 70-300mm F4-5.6 APO DG Macro HSM"}, //22
    {0xA33C5C8E303C4B0E, "Sigma 70-300mm F4-5.6 DG OS"},
    {0x02375E8E353D0200, "Sigma 75-300mm F4.5-5.6 APO"},
    {0x023A5E8E323D0200, "Sigma 75-300mm F4.0-5.6"},
    {0x77446198343C7B0E, "Sigma 80-400mm F4.5-5.6 EX OS"},
    {0x77446098343C7B0E, "Sigma 80-400mm F4.5-5.6 APO DG D OS"},
    {0x4848688E30304B02, "Sigma APO 100-300mm F4 EX IF HSM"},
    {0xF348688E30304B02, "Sigma APO 100-300mm F4 EX IF HSM"},
    {0x2645688E34421C02, "Sigma 100-300mm F4.5-6.7 DL"}, //30
    {0x48546F8E24244B02, "Sigma APO 120-300mm F2.8 EX DG HSM"},
    {0x7A546E8E24244B02, "Sigma APO 120-300mm F2.8 EX DG HSM"},
    {0xFA546E8E24244B02, "Sigma APO 120-300mm F2.8 EX DG HSM"}, //https://exiftool.org/forum/index.php/topic,2787.0.html
    {0xCF386E98343C4B0E, "Sigma APO 120-400mm F4.5-5.6 DG OS HSM"},
    {0xC334689838404B4E, "Sigma 100-400mm F5-6.3 DG OS HSM | C"}, //JR (017)
    {0x8D486E8E24244B0E, "Sigma 120-300mm F2.8 DG OS HSM Sports"},
    {0x26447398343C1C02, "Sigma 135-400mm F4.5-5.6 APO Aspherical"},
    {0xCE3476A038404B0E, "Sigma 150-500mm F5-6.3 DG OS APO HSM"}, //JD
    {0x813476A638404B0E, "Sigma 150-600mm F5-6.3 DG OS HSM | S"}, //Jaap Voets
    {0x823476A638404B0E, "Sigma 150-600mm F5-6.3 DG OS HSM | C"},
    {0xC44C737314144B46, "Sigma 135mm F1.8 DG HSM | A"}, //forum3833
    {0x26407BA034401C02, "Sigma APO 170-500mm F5-6.3 Aspherical RF"},
    {0xA74980A024244B06, "Sigma APO 200-500mm F2.8 EX DG"},
    {0x483C8EB03C3C4B02, "Sigma APO 300-800mm F5.6 EX DG HSM"},
    {0xD23C8EB03C3C4B02, "Sigma APO 300-800mm F5.6 EX DG HSM"}, //forum10942
//
    {0x0047252524240002, "Tamron SP AF 14mm f/2.8 Aspherical (IF) (69E)"},
    {0xC85444440D0DDF46, "Tamron SP 35mm f/1.4 Di USD (F045)"}, //IB
    {0xE84C44441414DF0E, "Tamron SP 35mm f/1.8 Di VC USD (F012)"}, //35
    {0xE74C4C4C1414DF0E, "Tamron SP 45mm f/1.8 Di VC USD (F013)"},
    {0xF454565618188406, "Tamron SP AF 60mm f/2.0 Di II Macro 1:1 (G005)"}, //24
    {0xE54C62621414C94E, "Tamron SP 85mm f/1.8 Di VC USD (F016)"}, //30
    {0x1E5D646420201300, "Tamron SP AF 90mm f/2.5 (52E)"},
    {0x205A646420201400, "Tamron SP AF 90mm f/2.5 Macro (152E)"},
    {0x225364642424E002, "Tamron SP AF 90mm f/2.8 Macro 1:1 (72E)"},
    {0x3253646424243502, "Tamron SP AF 90mm f/2.8 [Di] Macro 1:1 (172E/272E)"},
    {0xF855646424248406, "Tamron SP AF 90mm f/2.8 Di Macro 1:1 (272NII)"},
    {0xF85464642424DF06, "Tamron SP AF 90mm f/2.8 Di Macro 1:1 (272NII)"},
    {0xFE5464642424DF0E, "Tamron SP 90mm f/2.8 Di VC USD Macro 1:1 (F004)"}, //Jurgen Sahlberg
    {0xE45464642424DF0E, "Tamron SP 90mm f/2.8 Di VC USD Macro 1:1 (F017)"}, //Rolf Probst
    {0x004C7C7C2C2C0002, "Tamron SP AF 180mm f/3.5 Di Model (B01)"},
    {0x21568E8E24241400, "Tamron SP AF 300mm f/2.8 LD-IF (60E)"},
    {0x27548E8E24241D02, "Tamron SP AF 300mm f/2.8 LD-IF (360E)"},
    {0xE14019362C35DF4E, "Tamron 10-24mm f/3.5-4.5 Di II VC HLD (B023)"},
    {0xF63F18372C348406, "Tamron SP AF 10-24mm f/3.5-4.5 Di II LD Aspherical (IF) (B001)"},
    {0xF63F18372C34DF06, "Tamron SP AF 10-24mm f/3.5-4.5 Di II LD Aspherical (IF) (B001)"}, //30
    {0x00361C2D343C0006, "Tamron SP AF 11-18mm f/4.5-5.6 Di II LD Aspherical (IF) (A13)"},
    {0xE948273E2424DF0E, "Tamron SP 15-30mm f/2.8 Di VC USD (A012)"}, //IB
    {0xCA48273E2424DF4E, "Tamron SP 15-30mm f/2.8 Di VC USD G2 (A041)"}, //IB
    {0xEA40298E2C40DF0E, "Tamron 16-300mm f/3.5-6.3 Di II VC PZD (B016)"}, // (removed AF designation, ref 37)
    {0x07462B4424300302, "Tamron SP AF 17-35mm f/2.8-4 Di LD Aspherical (IF) (A05)"},
    {0xCB3C2B442431DF46, "Tamron 17-35mm f/2.8-4 Di OSD (A037)"}, //IB
    {0x00532B5024240006, "Tamron SP AF 17-50mm f/2.8 XR Di II LD Aspherical (IF) (A16)"}, //PH
    {0x7C542B5024240006, "Tamron SP AF 17-50mm f/2.8 XR Di II LD Aspherical (IF) (A16)"}, //PH (https://github.com/Exiv2/exiv2/issues/1155)
    {0x00542B5024240006, "Tamron SP AF 17-50mm f/2.8 XR Di II LD Aspherical (IF) (A16NII)"},
    {0xFB542B5024248406, "Tamron SP AF 17-50mm f/2.8 XR Di II LD Aspherical (IF) (A16NII)"}, //https://exiftool.org/forum/index.php/topic,3787.0.html
    {0xF3542B502424840E, "Tamron SP AF 17-50mm f/2.8 XR Di II VC LD Aspherical (IF) (B005)"},
    {0x003F2D802B400006, "Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical (IF) (A14)"},
    {0x003F2D802C400006, "Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical (IF) Macro (A14)"},
    {0xEC3E3C8E2C40DF0E, "Tamron 28-300mm f/3.5-6.3 Di VC PZD A010"}, //30
    {0x00402D802C400006, "Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical (IF) Macro (A14NII)"}, //NJ
    {0xFC402D802C40DF06, "Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical (IF) Macro (A14NII)"}, //PH (NC)
    {0xE6402D802C40DF0E, "Tamron 18-200mm f/3.5-6.3 Di II VC (B018)"}, //Tanel (removed AF designation, ref 37)
    {0x00402D882C406206, "Tamron AF 18-250mm f/3.5-6.3 Di II LD Aspherical (IF) Macro (A18)"},
    {0x00402D882C400006, "Tamron AF 18-250mm f/3.5-6.3 Di II LD Aspherical (IF) Macro (A18NII)"}, //JD
    {0xF5402C8A2C40400E, "Tamron AF 18-270mm f/3.5-6.3 Di II VC LD Aspherical (IF) Macro (B003)"},
    {0xF03F2D8A2C40DF0E, "Tamron AF 18-270mm f/3.5-6.3 Di II VC PZD (B008)"},
    {0xE0402D982C41DF4E, "Tamron 18-400mm f/3.5-6.3 Di II VC HLD (B028)"}, // (removed AF designation, ref 37)
    {0x07402F442C340302, "Tamron AF 19-35mm f/3.5-4.5 (A10)"},
    {0x074030452D350302, "Tamron AF 19-35mm f/3.5-4.5 (A10)"}, // there were two of them, one commented out
    {0x00493048222B0002, "Tamron SP AF 20-40mm f/2.7-3.5 (166D)"},
    {0x0E4A3148232D0E02, "Tamron SP AF 20-40mm f/2.7-3.5 (166D)"},
    {0xFE48375C2424DF0E, "Tamron SP 24-70mm f/2.8 Di VC USD (A007)"}, //24
    {0xCE47375C2525DF4E, "Tamron SP 24-70mm f/2.8 Di VC USD G2 (A032)"}, //forum9110
    {0x454137722C3C4802, "Tamron SP AF 24-135mm f/3.5-5.6 AD Aspherical (IF) Macro (190D)"},
    {0x33543C5E24246202, "Tamron SP AF 28-75mm f/2.8 XR Di LD Aspherical (IF) Macro (A09)"},
    {0xFA543C5E24248406, "Tamron SP AF 28-75mm f/2.8 XR Di LD Aspherical (IF) Macro (A09NII)"}, //JD
    {0xFA543C5E2424DF06, "Tamron SP AF 28-75mm f/2.8 XR Di LD Aspherical (IF) Macro (A09NII)"},
    {0x103D3C602C3CD202, "Tamron AF 28-80mm f/3.5-5.6 Aspherical (177D)"},
    {0x453D3C602C3C4802, "Tamron AF 28-80mm f/3.5-5.6 Aspherical (177D)"},
    {0x00483C6A24240002, "Tamron SP AF 28-105mm f/2.8 LD Aspherical IF (176D)"},
    {0x4D3E3C802E3C6202, "Tamron AF 28-200mm f/3.8-5.6 XR Aspherical (IF) Macro (A03N)"},
    {0x0B3E3D7F2F3D0E00, "Tamron AF 28-200mm f/3.8-5.6 (71D)"},
    {0x0B3E3D7F2F3D0E02, "Tamron AF 28-200mm f/3.8-5.6D (171D)"},
    {0x123D3C802E3CDF02, "Tamron AF 28-200mm f/3.8-5.6 AF Aspherical LD (IF) (271D)"},
    {0x4D413C8E2B406202, "Tamron AF 28-300mm f/3.5-6.3 XR Di LD Aspherical (IF) (A061)"},
    {0x4D413C8E2C406202, "Tamron AF 28-300mm f/3.5-6.3 XR LD Aspherical (IF) (185D)"},
    {0xF9403C8E2C40400E, "Tamron AF 28-300mm f/3.5-6.3 XR Di VC LD Aspherical (IF) Macro (A20)"},
    {0xC93C44762531DF4E, "Tamron 35-150mm f/2.8-4 Di VC OSD (A043)"}, //30
    {0x00475380303C0006, "Tamron AF 55-200mm f/4-5.6 Di II LD (A15)"},
    {0xF7535C8024248406, "Tamron SP AF 70-200mm f/2.8 Di LD (IF) Macro (A001)"},
    {0xFE535C8024248406, "Tamron SP AF 70-200mm f/2.8 Di LD (IF) Macro (A001)"},
    {0xF7535C8024244006, "Tamron SP AF 70-200mm f/2.8 Di LD (IF) Macro (A001)"},
  // {0xFE545C802424DF0E, "Tamron SP AF 70-200mm f/2.8 Di VC USD (A009)"},
    {0xFE545C802424DF0E, "Tamron SP 70-200mm f/2.8 Di VC USD (A009)"}, //NJ
    {0xE2475C802424DF4E, "Tamron SP 70-200mm f/2.8 Di VC USD G2 (A025)"}, //forum9549
    {0x69485C8E303C6F02, "Tamron AF 70-300mm f/4-5.6 LD Macro 1:2 (572D/772D)"},
    {0x69475C8E303C0002, "Tamron AF 70-300mm f/4-5.6 Di LD Macro 1:2 (A17N)"},
    {0x00485C8E303C0006, "Tamron AF 70-300mm f/4-5.6 Di LD Macro 1:2 (A17NII)"}, //JD
    {0xF1475C8E303CDF0E, "Tamron SP 70-300mm f/4-5.6 Di VC USD (A005)"},
    {0xCF475C8E313DDF0E, "Tamron SP 70-300mm f/4-5.6 Di VC USD (A030)"}, //forum9773
    {0xCC4468983441DF0E, "Tamron 100-400mm f/4.5-6.3 Di VC USD"}, //30
    {0xEB4076A63840DF0E, "Tamron SP AF 150-600mm f/5-6.3 VC USD (A011)"},
    {0xE34076A63840DF4E, "Tamron SP 150-600mm f/5-6.3 Di VC USD G2"}, //30
    {0xE34076A63840DF0E, "Tamron SP 150-600mm f/5-6.3 Di VC USD G2 (A022)"}, //forum3833
    {0x203C80983D3D1E02, "Tamron AF 200-400mm f/5.6 LD IF (75D)"},
    {0x003E80A0383F0002, "Tamron SP AF 200-500mm f/5-6.3 Di LD (IF) (A08)"},
    {0x003F80A0383F0002, "Tamron SP AF 200-500mm f/5-6.3 Di (A08)"},
//
    {0x00402B2B2C2C0002, "Tokina AT-X 17 AF PRO (AF 17mm f/3.5)"},
    {0x0047444424240006, "Tokina AT-X M35 PRO DX (AF 35mm f/2.8 Macro)"},
    {0x8D54686824248702, "Tokina AT-X PRO 100mm F2.8 D Macro"}, //30
    {0x0054686824240002, "Tokina AT-X M100 AF PRO D (AF 100mm f/2.8 Macro)"},
    {0x27488E8E30301D02, "Tokina AT-X 304 AF (AF 300mm f/4.0)"},
    {0x00548E8E24240002, "Tokina AT-X 300 AF PRO (AF 300mm f/2.8)"},
    {0x123B98983D3D0900, "Tokina AT-X 400 AF SD (AF 400mm f/5.6)"},
    {0x0040182B2C340006, "Tokina AT-X 107 AF DX Fisheye (AF 10-17mm f/3.5-4.5)"},
    {0x00481C2924240006, "Tokina AT-X 116 PRO DX (AF 11-16mm f/2.8)"},
    {0x7A481C2924247E06, "Tokina AT-X 116 PRO DX II (AF 11-16mm f/2.8)"},
    {0x80481C2924247A06, "Tokina atx-i 11-16mm F2.8 CF"}, //exiv2 issue 1078
    {0x7A481C3024247E06, "Tokina AT-X 11-20 F2.8 PRO DX (AF 11-20mm f/2.8)"},
    {0x8B481C3024248506, "Tokina AT-X 11-20 F2.8 PRO DX (AF 11-20mm f/2.8)"}, //forum12687
    {0x003C1F3730300006, "Tokina AT-X 124 AF PRO DX (AF 12-24mm f/4)"},
    // '7A 3C 1F 37 30 30 7E 06.2' => 'Tokina AT-X 124 AF PRO DX II (AF 12-24mm f/4)',
    {0x7A3C1F3C30307E06, "Tokina AT-X 12-28 PRO DX (AF 12-28mm f/4)"},
    {0x0048293C24240006, "Tokina AT-X 16-28 AF PRO FX (AF 16-28mm f/2.8)"},
    {0x0048295024240006, "Tokina AT-X 165 PRO DX (AF 16-50mm f/2.8)"},
    {0x00402A722C3C0006, "Tokina AT-X 16.5-135 DX (AF 16.5-135mm F3.5-5.6)"},
    {0x003C2B4430300006, "Tokina AT-X 17-35 F4 PRO FX (AF 17-35mm f/4)"},
    // '2F 40 30 44 2C 34 29 02.2' => 'Tokina AF 193 (AF 19-35mm f/3.5-4.5)',
    // '2F 48 30 44 24 24 29 02.2' => 'Tokina AT-X 235 AF PRO (AF 20-35mm f/2.8)',
    {0x2F4030442C342902, "Tokina AF 235 II (AF 20-35mm f/3.5-4.5)"}, // there were two of them, one commented out
    {0x0048375C24240006, "Tokina AT-X 24-70 F2.8 PRO FX (AF 24-70mm f/2.8)"},
    {0x004037802C3C0002, "Tokina AT-X 242 AF (AF 24-200mm f/3.5-5.6)"},
    {0x25483C5C24241B02, "Tokina AT-X 270 AF PRO II (AF 28-70mm f/2.6-2.8)"}, // there were two of them, one commented out
    // '25 48 3C 5C 24 24 1B 02.2' => 'Tokina AT-X 287 AF PRO SV (AF 28-70mm f/2.8)',
    {0x07483C5C24240300, "Tokina AT-X 287 AF (AF 28-70mm f/2.8)"},
    {0x07473C5C25350300, "Tokina AF 287 SD (AF 28-70mm f/2.8-4.5)"},
    {0x07403C5C2C350300, "Tokina AF 270 II (AF 28-70mm f/3.5-4.5)"},
    {0x00483C6024240002, "Tokina AT-X 280 AF PRO (AF 28-80mm f/2.8)"},
    {0x2544448E34421B02, "Tokina AF 353 (AF 35-300mm f/4.5-6.7)"},
    {0x0048507224240006, "Tokina AT-X 535 PRO DX (AF 50-135mm f/2.8)"},
    {0x003C5C803030000E, "Tokina AT-X 70-200 F4 FX VCM-S (AF 70-200mm f/4)"},
    {0x00485C803030000E, "Tokina AT-X 70-200 F4 FX VCM-S (AF 70-200mm f/4)"},
    {0x12445E8E343C0900, "Tokina AF 730 (AF 75-300mm F4.5-5.6)"},
    {0x1454608024240B00, "Tokina AT-X 828 AF (AF 80-200mm f/2.8)"},
    {0x2454608024241A02, "Tokina AT-X 828 AF PRO (AF 80-200mm f/2.8)"},
    {0x24446098343C1A02, "Tokina AT-X 840 AF-II (AF 80-400mm f/4.5-5.6)"},
    {0x00446098343C0002, "Tokina AT-X 840 D (AF 80-400mm f/4.5-5.6)"},
    {0x1448688E30300B00, "Tokina AT-X 340 AF (AF 100-300mm f/4)"},
    {0x8C48293C24248606, "Tokina opera 16-28mm F2.8 FF"}, //30
//
    {0x063F68682C2C0600, "Cosina AF 100mm F3.5 Macro"},
    {0x07363D5F2C3C0300, "Cosina AF Zoom 28-80mm F3.5-5.6 MC Macro"},
    {0x07463D6A252F0300, "Cosina AF Zoom 28-105mm F2.8-3.8 MC"},
    {0x12365C81353D0900, "Cosina AF Zoom 70-210mm F4.5-5.6 MC Macro"},
    {0x12395C8E343D0802, "Cosina AF Zoom 70-300mm F4.5-5.6 MC Macro"},
    {0x123B688D3D430902, "Cosina AF Zoom 100-300mm F5.6-6.7 MC Macro"},
//
    {0x1238699735420902, "Promaster Spectrum 7 100-400mm F4.5-6.7"},
//
    {0x004031312C2C0000, "Voigtlander Color Skopar 20mm F3.5 SLII Aspherical"},
    {0x00483C3C24240000, "Voigtlander Color Skopar 28mm F2.8 SL II"},
    {0x0054484818180000, "Voigtlander Ultron 40mm F2 SLII Aspherical"},
    {0x005455550C0C0000, "Voigtlander Nokton 58mm F1.4 SLII"},
    {0x004064642C2C0000, "Voigtlander APO-Lanthar 90mm F3.5 SLII Close Focus"},
    // '07 40 30 45 2D 35 03 02.2' => 'Voigtlander Ultragon 19-35mm F3.5-4.5 VMV', //NJ
    {0x7148646424240000, "Voigtlander APO-Skopar 90mm F2.8 SL IIs"}, //30
    {0xFD0050501818DF00, "Voigtlander APO-Lanthar 50mm F2 Aspherical"}, //35
    {0xFD0044441818DF00, "Voigtlander APO-Lanthar 35mm F2"}, //30
    {0xFD0059591818DF00, "Voigtlander Macro APO-Lanthar 65mm F2"}, //30
    {0xFD0048480707DF00, "Voigtlander Nokton 40mm F1.2 Aspherical"}, //30
//
    {0x00402D2D2C2C0000, "Carl Zeiss Distagon T* 3.5/18 ZF.2"},
    {0x0048272724240000, "Carl Zeiss Distagon T* 2.8/15 ZF.2"}, //MykytaKozlov
    {0x0048323224240000, "Carl Zeiss Distagon T* 2.8/21 ZF.2"},
    {0x0054383818180000, "Carl Zeiss Distagon T* 2/25 ZF.2"},
    {0x00543C3C18180000, "Carl Zeiss Distagon T* 2/28 ZF.2"},
    {0x005444440C0C0000, "Carl Zeiss Distagon T* 1.4/35 ZF.2"},
    {0x0054444418180000, "Carl Zeiss Distagon T* 2/35 ZF.2"},
    {0x005450500C0C0000, "Carl Zeiss Planar T* 1.4/50 ZF.2"},
    {0x0054505018180000, "Carl Zeiss Makro-Planar T* 2/50 ZF.2"},
    {0x005462620C0C0000, "Carl Zeiss Planar T* 1.4/85 ZF.2"},
    {0x0054686818180000, "Carl Zeiss Makro-Planar T* 2/100 ZF.2"},
    {0x0054727218180000, "Carl Zeiss Apo Sonnar T* 2/135 ZF.2"},
    {0x02543C3C0C0C0000, "Zeiss Otus 1.4/28 ZF.2"}, //30
    {0x005453530C0C0000, "Zeiss Otus 1.4/55"}, //IB
    {0x015462620C0C0000, "Zeiss Otus 1.4/85"},
    {0x035468680C0C0000, "Zeiss Otus 1.4/100"}, //IB
    {0x5254444418180000, "Zeiss Milvus 35mm f/2"},
    {0x535450500C0C0000, "Zeiss Milvus 50mm f/1.4"}, //IB
    {0x5454505018180000, "Zeiss Milvus 50mm f/2 Macro"},
    {0x555462620C0C0000, "Zeiss Milvus 85mm f/1.4"}, //IB
    {0x5654686818180000, "Zeiss Milvus 100mm f/2 Macro"},
//
    {0x0054565630300000, "Coastal Optical Systems 60mm 1:4 UV-VIS-IR Macro Apo"},
//
    {0xBF4E26261E1E0104, "Irix 15mm f/2.4 Firefly"}, //30 (guessing the Blackstone version may be the same ID - PH)
    {0xBF3C1B1B30300104, "Irix 11mm f/4 Firefly"}, //30 (guessing the Blackstone version may be the same ID - PH)
//
    {0x4A4011112C0C4D02, "Samyang 8mm f/3.5 Fish-Eye CS"},
    {0x4A482424240C4D02, "Samyang 10mm f/2.8 ED AS NCS CS"}, // there were two of them, one commented out
    {0x4A481E1E240C4D02, "Samyang 12mm f/2.8 ED AS NCS Fish-Eye"}, //Jurgen Sahlberg
    // '4A 48 24 24 24 0C 4D 02.2' => 'Samyang AE 14mm f/2.8 ED AS IF UMC', //https://exiftool.org/forum/index.php/topic,3150.0.html
    {0x4A4C24241E6C4D06, "Samyang 14mm f/2.4 Premium"},
    {0x4A542929180C4D02, "Samyang 16mm f/2.0 ED AS UMC CS"}, //Jon Bloom (by email)
    {0x4A6036360C0C4D02, "Samyang 24mm f/1.4 ED AS UMC"},
    {0x4A6044440C0C4D02, "Samyang 35mm f/1.4 AS UMC"},
    {0x4A6062620C0C4D02, "Samyang AE 85mm f/1.4 AS IF UMC"}, //https://exiftool.org/forum/index.php/topic,2888.0.html
//
    {0x9A4C505014149C06, "Yongnuo YN50mm F1.8N"},
    {0x9F4848482424A106, "Yongnuo YN40mm F2.8N"}, //30
    {0x9F5468681818A206, "Yongnuo YN100mm F2N"}, //30
    {0x9F4C44441818A106, "Yongnuo YN35mm F2"}, //30
//
    {0x0240445C2C340200, "Exakta AF 35-70mm 1:3.5-4.5 MC"},
//
    {0x073E30432D350300, "Soligor AF Zoom 19-35mm 1:3.5-4.5 MC"},
    {0x03435C8135350200, "Soligor AF C/D Zoom UMCS 70-210mm 1:4.5"},
    {0x124A5C81313D0900, "Soligor AF C/D Auto Zoom+Macro 70-210mm 1:4-5.6 UMCS"},
    {0x1236699735420900, "Soligor AF Zoom 100-400mm 1:4.5-6.7 MC"},
//
    {0x0000000000000001, "Manual Lens No CPU"},
//
    {0x0000484853530001, "Loreo 40mm F11-22 3D Lens in a Cap 9005"}, //PH
    {0x0047101024240000, "Fisheye Nikkor 8mm f/2.8 AiS"},
    {0x00473C3C24240000, "Nikkor 28mm f/2.8 AiS"}, //35
  // {0x005444440C0C0000, "Nikkor 35mm f/1.4 AiS"}, comment out in favour of Zeiss with same ID because this lens is rare (requires CPU upgrade)
    {0x0057505014140000, "Nikkor 50mm f/1.8 AI"}, //35
    {0x0048505018180000, "Nikkor H 50mm f/2"},
    {0x0048686824240000, "Series E 100mm f/2.8"},
    {0x004C6A6A20200000, "Nikkor 105mm f/2.5 AiS"},
    {0x0048808030300000, "Nikkor 200mm f/4 AiS"},
    {0x004011112C2C0000, "Samyang 8mm f/3.5 Fish-Eye"},
    {0x0058646420200000, "Soligor C/D Macro MC 90mm f/2.5"},
    {0x4A583030140C4D02, "Rokinon 20mm f/1.8 ED AS UMC"}, //30
//
    {0xA05644441414A206, "Sony FE 35mm F1.8"}, //IB (Techart adapter)
    {0xA0375C8E343CA206, "Sony FE 70-300mm F4.5-5.6 G OSS"}, //IB (Techart adapter)
    };
    auto it = lensDB.find(lensId);
    if (it != lensDB.end()) {
        return it.value();
    }
    return QString();
}
