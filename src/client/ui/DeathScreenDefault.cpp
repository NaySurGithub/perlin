#include "client/ui/DeathScreen.hpp"

namespace perlin::ui {
    std::string EmbeddedDeathScreenTemplate() {
        std::string t;
        t += R"PERLINRML(<rml>
<head>
<title>death</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
.t { display: block; position: absolute; font-family: minecraft-shadow; white-space: nowrap; overflow: visible; z-index: 3;
      left: 0px; width: {{w}}px; text-align: center; color: #ffffff; }
.strip { display: block; position: absolute; left: 0px; width: 100%; z-index: 1; }
#heading { top: 60px; height: 17px; font-size: 16px; line-height: 17px; }
#cause { top: 85px; height: 9px; font-size: 8px; line-height: 9px; }
.btn { display: block; position: absolute; width: 200px; height: 20px; background-color: #000000; z-index: 5; }
.btn div { display: block; position: absolute; }
.btn .rim { left: 1px; top: 1px; width: 197px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; width: 196px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; width: 198px; height: 2px; background-color: #565656; }
.btn .edge { left: 198px; top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; width: 200px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
</style>
</head>
<body>
<!-- fillGradient(0, 0, w, h, 0x60500000, 0xA0803030) as thin strips -->
  <div class="strip" style="top:0.0000%;height:2.2833%;background-color:rgba(80,0,0,97);"></div>
  <div class="strip" style="top:2.0833%;height:2.2833%;background-color:rgba(82,2,2,98);"></div>
  <div class="strip" style="top:4.1667%;height:2.2833%;background-color:rgba(82,2,2,99);"></div>
  <div class="strip" style="top:6.2500%;height:2.2833%;background-color:rgba(84,4,4,101);"></div>
  <div class="strip" style="top:8.3333%;height:2.2833%;background-color:rgba(84,4,4,102);"></div>
  <div class="strip" style="top:10.4167%;height:2.2833%;background-color:rgba(86,6,6,103);"></div>
  <div class="strip" style="top:12.5000%;height:2.2833%;background-color:rgba(86,6,6,105);"></div>
  <div class="strip" style="top:14.5833%;height:2.2833%;background-color:rgba(88,8,8,106);"></div>
  <div class="strip" style="top:16.6667%;height:2.2833%;background-color:rgba(88,8,8,107);"></div>
  <div class="strip" style="top:18.7500%;height:2.2833%;background-color:rgba(90,10,10,109);"></div>
  <div class="strip" style="top:20.8333%;height:2.2833%;background-color:rgba(90,10,10,110);"></div>
  <div class="strip" style="top:22.9167%;height:2.2833%;background-color:rgba(92,12,12,111);"></div>
  <div class="strip" style="top:25.0000%;height:2.2833%;background-color:rgba(92,12,12,113);"></div>
  <div class="strip" style="top:27.0833%;height:2.2833%;background-color:rgba(94,14,14,114);"></div>
  <div class="strip" style="top:29.1667%;height:2.2833%;background-color:rgba(94,14,14,115);"></div>
  <div class="strip" style="top:31.2500%;height:2.2833%;background-color:rgba(96,16,16,117);"></div>
  <div class="strip" style="top:33.3333%;height:2.2833%;background-color:rgba(96,16,16,118);"></div>
  <div class="strip" style="top:35.4167%;height:2.2833%;background-color:rgba(98,18,18,119);"></div>
  <div class="strip" style="top:37.5000%;height:2.2833%;background-color:rgba(98,18,18,121);"></div>
  <div class="strip" style="top:39.5833%;height:2.2833%;background-color:rgba(100,20,20,122);"></div>
  <div class="strip" style="top:41.6667%;height:2.2833%;background-color:rgba(100,20,20,123);"></div>
  <div class="strip" style="top:43.7500%;height:2.2833%;background-color:rgba(102,22,22,125);"></div>
  <div class="strip" style="top:45.8333%;height:2.2833%;background-color:rgba(102,22,22,126);"></div>
  <div class="strip" style="top:47.9167%;height:2.2833%;background-color:rgba(104,24,24,127);"></div>
  <div class="strip" style="top:50.0000%;height:2.2833%;background-color:rgba(104,24,24,129);"></div>
  <div class="strip" style="top:52.0833%;height:2.2833%;background-color:rgba(106,26,26,130);"></div>
  <div class="strip" style="top:54.1667%;height:2.2833%;background-color:rgba(106,26,26,131);"></div>
  <div class="strip" style="top:56.2500%;height:2.2833%;background-color:rgba(108,28,28,133);"></div>
  <div class="strip" style="top:58.3333%;height:2.2833%;background-color:rgba(108,28,28,134);"></div>
  <div class="strip" style="top:60.4167%;height:2.2833%;background-color:rgba(110,30,30,135);"></div>
  <div class="strip" style="top:62.5000%;height:2.2833%;background-color:rgba(110,30,30,137);"></div>
  <div class="strip" style="top:64.5833%;height:2.2833%;background-color:rgba(112,32,32,138);"></div>
  <div class="strip" style="top:66.6667%;height:2.2833%;background-color:rgba(112,32,32,139);"></div>
  <div class="strip" style="top:68.7500%;height:2.2833%;background-color:rgba(114,34,34,141);"></div>
  <div class="strip" style="top:70.8333%;height:2.2833%;background-color:rgba(114,34,34,142);"></div>
  <div class="strip" style="top:72.9167%;height:2.2833%;background-color:rgba(116,36,36,143);"></div>
  <div class="strip" style="top:75.0000%;height:2.2833%;background-color:rgba(116,36,36,145);"></div>
  <div class="strip" style="top:77.0833%;height:2.2833%;background-color:rgba(118,38,38,146);"></div>
  <div class="strip" style="top:79.1667%;height:2.2833%;background-color:rgba(118,38,38,147);"></div>
  <div class="strip" style="top:81.2500%;height:2.2833%;background-color:rgba(120,40,40,149);"></div>
  <div class="strip" style="top:83.3333%;height:2.2833%;background-color:rgba)PERLINRML";
        t += R"PERLINRML((120,40,40,150);"></div>
  <div class="strip" style="top:85.4167%;height:2.2833%;background-color:rgba(122,42,42,151);"></div>
  <div class="strip" style="top:87.5000%;height:2.2833%;background-color:rgba(122,42,42,153);"></div>
  <div class="strip" style="top:89.5833%;height:2.2833%;background-color:rgba(124,44,44,154);"></div>
  <div class="strip" style="top:91.6667%;height:2.2833%;background-color:rgba(124,44,44,155);"></div>
  <div class="strip" style="top:93.7500%;height:2.2833%;background-color:rgba(126,46,46,157);"></div>
  <div class="strip" style="top:95.8333%;height:2.2833%;background-color:rgba(126,46,46,158);"></div>
  <div class="strip" style="top:97.9167%;height:2.2833%;background-color:rgba(128,48,48,159);"></div>
  <div id="heading" class="t">{{title}}</div>
  <div id="cause" class="t">{{cause}}</div>
  <div id="respawn" class="btn{{off}}" style="left: {{btn_x}}px; top: {{btn1_y}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{respawn}}</div>
  </div>
  <div id="title" class="btn{{off}}" style="left: {{btn_x}}px; top: {{btn2_y}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{title_screen}}</div>
  </div>
</body>
</rml>
)PERLINRML";
        return t;
    }
}
