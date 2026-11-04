/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Telewhite {

// The default palette with violet accents, the rest comes from fallbacks.
inline constexpr auto kLightPalette = std::string_view(R"PALETTE(
windowBgActive: #6f4cd7;
windowActiveTextFg: #4b24bf;
activeButtonBgOver: #6845cf;
activeButtonBgRipple: #532dc3;
activeButtonSecondaryFg: #dbd0fb;
activeLineFg: #6744d1;
lightButtonBgOver: #eae5f8;
lightButtonBgRipple: #d6ccf3;
filterInputBorderFg: #8260e7;
botKbPrimaryBg: #5935c3cc;
dialogsBgActive: #6d4cce;
dialogsDraftFgActive: #d4caf3;
historyUnreadBarFg: #6f5aad;
msgInBgSelected: #d5c6ee;
msgSelectOverlay: #6341c84c;
msgStickerOverlay: #6341c87f;
msgInShadowSelected: #7c5cb329;
msgInDateFgSelected: #8d71be;
msgFileInBgOver: #7959d8;
msgFileInBgSelected: #835bc9;
msgWaveformInActiveSelected: #835bc9;
msgWaveformInInactiveSelected: #b7a1dc;
overviewPhotoSelectOverlay: #6f4cd733;
mainMenuCloudBg: #5332b4;
mediaPlayerDisabledFg: #b5a3e9;
sideBarTextFgActive: #8f6fef;
sideBarIconFgActive: #8a69ec;
sideBarBadgeBg: #8a69ec;
dialogsMentionIconFg: #6f4cd7;
introCoverTopBg: #461dc2;
introCoverBottomBg: #6e47e2;
introCoverIconsFg: #8c6af3;
introCoverPlaneTrace: #8c6af369;
dialogsSentIconFg: #6f52c4;
dialogsSentIconFgOver: #684db8;
historyOutIconFg: #674cb8;
historyOutIconFgSelected: #6a45aa;
historySendingOutIconFg: #a292d2;
msgOutBg: #e6defd;
msgOutBgSelected: #c4b7db;
msgOutServiceFg: #4b2da3;
msgOutServiceFgSelected: #644699;
msgOutShadow: #5c3ac31d;
msgOutShadowSelected: #6037a722;
msgOutDateFg: #7a66b5;
msgOutDateFgSelected: #7856b2;
msgOutReplyBarColor: #6d54b8;
msgOutMonoFg: #5a4598;
msgFileThumbLinkOutFg: #4f31a8;
msgFileThumbLinkOutFgSelected: #5a31a2;
msgFileOutBg: #775fbe;
msgFileOutBgSelected: #7250ac;
msgWaveformOutActive: #765ebd;
msgWaveformOutActiveSelected: #836bad;
msgWaveformOutInactive: #bfb3e2;
msgWaveformOutInactiveSelected: #a391c3;
)PALETTE");

// The default palette with pink accents.
inline constexpr auto kSakuraPalette = std::string_view(R"PALETTE(
windowBgActive: #d6467f;
windowActiveTextFg: #be2260;
activeButtonBgOver: #ce3f78;
activeButtonBgRipple: #c22966;
activeButtonSecondaryFg: #facee0;
activeLineFg: #d03e7b;
lightButtonBgOver: #f7e5ec;
lightButtonBgRipple: #f2cbdb;
filterInputBorderFg: #e65992;
botKbPrimaryBg: #c2316bcc;
dialogsBgActive: #cd477c;
dialogsDraftFgActive: #f2c7d9;
historyUnreadBarFg: #ac5679;
msgInBgSelected: #edc4d0;
msgSelectOverlay: #c73b734c;
msgStickerOverlay: #c73b737f;
msgInShadowSelected: #b2587129;
msgInDateFgSelected: #bd6d85;
msgFileInBgOver: #d75388;
msgFileInBgSelected: #c85678;
msgWaveformInActiveSelected: #c85678;
msgWaveformInInactiveSelected: #db9eaf;
overviewPhotoSelectOverlay: #d6467f33;
mainMenuCloudBg: #b32e63;
mediaPlayerDisabledFg: #e8a0bd;
sideBarTextFgActive: #ee699e;
sideBarIconFgActive: #eb649a;
sideBarBadgeBg: #eb649a;
dialogsMentionIconFg: #d6467f;
introCoverTopBg: #c11b60;
introCoverBottomBg: #e14181;
introCoverIconsFg: #f265a0;
introCoverPlaneTrace: #f265a069;
dialogsSentIconFg: #c34c7c;
dialogsSentIconFgOver: #b74874;
historyOutIconFg: #b74774;
historyOutIconFgSelected: #a94060;
historySendingOutIconFg: #d18faa;
msgOutBg: #fcdce9;
msgOutBgSelected: #dab5c0;
msgOutServiceFg: #a2295a;
msgOutServiceFgSelected: #98425c;
msgOutShadow: #c235701d;
msgOutShadowSelected: #a6325522;
msgOutDateFg: #b46283;
msgOutDateFgSelected: #b1526d;
msgOutReplyBarColor: #b75079;
msgOutMonoFg: #974163;
msgFileThumbLinkOutFg: #a72d5e;
msgFileThumbLinkOutFgSelected: #a12d50;
msgFileOutBg: #bd5a82;
msgFileOutBgSelected: #ab4c67;
msgWaveformOutActive: #bc5981;
msgWaveformOutActiveSelected: #ac687c;
msgWaveformOutInactive: #e1b0c4;
msgWaveformOutInactiveSelected: #c28e9e;
)PALETTE");

// The default palette with mint green accents.
inline constexpr auto kMintPalette = std::string_view(R"PALETTE(
windowBgActive: #209e78;
windowActiveTextFg: #0f8c67;
activeButtonBgOver: #1d9873;
activeButtonBgRipple: #138f6a;
activeButtonSecondaryFg: #c2f5e6;
activeLineFg: #1c9971;
lightButtonBgOver: #d9eee8;
lightButtonBgRipple: #b6e1d4;
filterInputBorderFg: #36c499;
botKbPrimaryBg: #168f6bcc;
dialogsBgActive: #219774;
dialogsDraftFgActive: #b2e1d2;
historyUnreadBarFg: #347f68;
msgInBgSelected: #aad5cd;
msgSelectOverlay: #1b926e4c;
msgStickerOverlay: #1b926e7f;
msgInShadowSelected: #34837529;
msgInDateFgSelected: #458b7d;
msgFileInBgOver: #2ca07d;
msgFileInBgSelected: #30937f;
msgWaveformInActiveSelected: #30937f;
msgWaveformInInactiveSelected: #72aaa0;
overviewPhotoSelectOverlay: #209e7833;
mainMenuCloudBg: #158463;
mediaPlayerDisabledFg: #7fc9b3;
sideBarTextFgActive: #49d8ad;
sideBarIconFgActive: #41d0a5;
sideBarBadgeBg: #41d0a5;
dialogsMentionIconFg: #209e78;
introCoverTopBg: #0c8e65;
introCoverBottomBg: #21b88b;
introCoverIconsFg: #45e1b0;
introCoverPlaneTrace: #45e1b069;
dialogsSentIconFg: #289071;
dialogsSentIconFgOver: #26876a;
historyOutIconFg: #25876a;
historyOutIconFgSelected: #217c6a;
historySendingOutIconFg: #619a89;
msgOutBg: #d5faef;
msgOutBgSelected: #86a7a0;
msgOutServiceFg: #137759;
msgOutServiceFgSelected: #257061;
msgOutShadow: #188f691d;
msgOutShadowSelected: #177a6622;
msgOutDateFg: #3d856f;
msgOutDateFgSelected: #2f8273;
msgOutReplyBarColor: #2c876c;
msgOutMonoFg: #246f59;
msgFileThumbLinkOutFg: #157b5c;
msgFileThumbLinkOutFgSelected: #157763;
msgFileOutBg: #358b71;
msgFileOutBgSelected: #2a7e6f;
msgWaveformOutActive: #348a70;
msgWaveformOutActiveSelected: #437f73;
msgWaveformOutInactive: #89b8aa;
msgWaveformOutInactiveSelected: #628f86;
)PALETTE");

} // namespace Telewhite
