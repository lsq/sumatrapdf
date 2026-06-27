/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/Dpi.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"
#include "utils/SquareTreeParser.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"
#include "wingui/VirtWnd.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "SumatraConfig.h"
#include "FileHistory.h"
#include "GlobalPrefs.h"
#include "Annotation.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "resource.h"
#include "Commands.h"
#include "Accelerators.h"
#include "CommandPalette.h"
#include "FileThumbnails.h"
#include "HomePage.h"
#include <wingdi.h>
#include "Translations.h"
#include "Version.h"
#include "Theme.h"
#include "AppSettings.h"
#include "OverlayScrollbar.h"
#include "DarkModeSubclass.h"
#include "utils/Log.h"
#include "AppTools.h"
#include "utils/HttpUtil.h"

#ifndef ABOUT_USE_LESS_COLORS
#define ABOUT_LINE_OUTER_SIZE 2
#else
#define ABOUT_LINE_OUTER_SIZE 1
#endif
#define ABOUT_LINE_SEP_SIZE 1

constexpr const char* sumatraTips = R"(You can [customize scrollbar](CmdChangeScrollbar).
You can [customize keyboard shortcuts](Help/Customizing-keyboard-shortcuts).
You can [customize toolbar](Help/Customize-toolbar).
Press (Key/CmdCommandPalette) to open [command palette](CmdCommandPalette).
To open file from history open [command palette](CmdCommandPalette) with (Key/CmdCommandPalette) and type `#`.
You can [extract text from PDF file](Help/Tool-x-extract-text-from-pdf).
You can [toggle menu bar](CmdToggleMenuBar) with (Key/CmdToggleMenuBar).
You can [toggle toolbar](CmdToggleToolbar) with (Key/CmdToggleToolbar).
You can [edit PDF annotations](Help/Editing-annotations).
)";

constexpr const char* sumatraPromos = R"(Try [Edna](https://edna.arslexis.io): a note taking web app for power users.
Try [MarkLexis](https://marklexis.arslexis.io): a bookmarking web application.
)";

// TODO: leaks if set
const char* promoFromServer = nullptr;

// a word in a parsed tip; can be part of a link
struct TipWord {
    char* text = nullptr; // owned
    int dx = 0;
    int dy = 0;
    int x = 0;
    int y = 0;
    bool isLink = false;
    int linkIdx = -1; // index into ParsedTip::links
};

struct TipLink {
    char* cmd = nullptr; // owned, the link_command
    int firstWord = 0;
    int lastWord = 0; // inclusive
};

struct ParsedTip {
    Vec<TipWord> words;
    Vec<TipLink> links;
    int totalDy = 0; // computed by layout

    ~ParsedTip() {
        for (auto& w : words) {
            str::Free(w.text);
        }
        for (auto& l : links) {
            str::Free(l.cmd);
        }
    }
};

// resolve (Key/CmdXxx) to keyboard shortcut string
static TempStr ResolveKeyShortcutTemp(const char* cmdName) {
    int cmdId = GetCommandIdByName(cmdName);
    if (cmdId <= 0) {
        return str::DupTemp(cmdName);
    }
    TempStr accel = AppendAccelKeyToMenuStringTemp((TempStr) "", cmdId);
    if (!accel || !*accel) {
        return str::DupTemp(cmdName);
    }
    // AppendAccelKeyToMenuStringTemp prepends \t, skip it
    if (accel[0] == '\t') {
        accel++;
    }
    return accel;
}

// resolve link command to a URL for StaticLink target
static TempStr ResolveLinkCmdTemp(const char* cmd) {
    if (str::StartsWith(cmd, "https://") || str::StartsWith(cmd, "http://")) {
        return str::DupTemp(cmd);
    }
    if (str::StartsWith(cmd, "Help/")) {
        return str::FormatTemp("https://www.sumatrapdfreader.org/docs/%s", cmd + 5);
    }
    // Cmd* - use as-is, will be resolved to command ID on click
    return str::DupTemp(cmd);
}

static void ParseTip(ParsedTip& tip, const char* s) {
    StrBuilder expanded;
    // first pass: expand (Key/CmdXxx) to shortcut strings
    while (*s) {
        if (*s == '(' && str::StartsWith(s + 1, "Key/")) {
            const char* end = str::FindChar(s, ')');
            if (end) {
                // extract command name between "Key/" and ")"
                const char* cmdStart = s + 5; // skip "(Key/"
                TempStr cmdName = str::DupTemp(cmdStart, (int)(end - cmdStart));
                TempStr shortcut = ResolveKeyShortcutTemp(cmdName);
                expanded.Append(shortcut);
                s = end + 1;
                continue;
            }
        }
        expanded.AppendChar(*s);
        s++;
    }

    // second pass: split into words, detecting [text](link) markdown links
    const char* p = expanded.Get();
    while (*p) {
        // skip spaces
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }

        if (*p == '[') {
            // parse markdown link: [text](cmd)
            const char* textStart = p + 1;
            const char* textEnd = str::FindChar(textStart, ']');
            if (textEnd && textEnd[1] == '(') {
                const char* cmdStart = textEnd + 2;
                const char* cmdEnd = str::FindChar(cmdStart, ')');
                if (cmdEnd) {
                    TempStr linkCmd = str::DupTemp(cmdStart, (int)(cmdEnd - cmdStart));
                    TempStr linkText = str::DupTemp(textStart, (int)(textEnd - textStart));

                    TipLink link;
                    link.cmd = str::Dup(ResolveLinkCmdTemp(linkCmd));
                    link.firstWord = tip.words.Size();

                    // split link text into words
                    const char* lt = linkText;
                    while (*lt) {
                        while (*lt == ' ') {
                            lt++;
                        }
                        if (!*lt) {
                            break;
                        }
                        const char* wordStart = lt;
                        while (*lt && *lt != ' ') {
                            lt++;
                        }
                        TipWord w;
                        w.text = str::Dup(wordStart, (int)(lt - wordStart));
                        w.isLink = true;
                        w.linkIdx = tip.links.Size();
                        tip.words.Append(w);
                    }

                    link.lastWord = tip.words.Size() - 1;
                    tip.links.Append(link);
                    p = cmdEnd + 1;
                    continue;
                }
            }
        }

        // regular word
        const char* wordStart = p;
        while (*p && *p != ' ' && *p != '[') {
            p++;
        }
        if (p > wordStart) {
            TipWord w;
            w.text = str::Dup(wordStart, (int)(p - wordStart));
            tip.words.Append(w);
        }
    }
}

static void MeasureTipWords(ParsedTip& tip, HDC hdc, HFONT font) {
    uint fmt = DT_LEFT | DT_NOCLIP;
    for (auto& w : tip.words) {
        Size sz = HdcMeasureText(hdc, w.text, fmt, font);
        w.dx = sz.dx;
        w.dy = sz.dy;
    }
}

static void LayoutTip(ParsedTip& tip, int areaWidth, int startX, int startY) {
    int x = startX;
    int y = startY;
    int lineHeight = 0;
    int spaceWidth = 4; // approximate space between words
    for (auto& w : tip.words) {
        if (x > startX && x + w.dx > startX + areaWidth) {
            // wrap to next line
            x = startX;
            y += lineHeight + 2;
            lineHeight = 0;
        }
        w.x = x;
        w.y = y;
        x += w.dx + spaceWidth;
        if (w.dy > lineHeight) {
            lineHeight = w.dy;
        }
    }
    tip.totalDy = (y - startY) + lineHeight;
}

static ParsedTip* gParsedTips = nullptr;
static int gParsedTipCount = 0;
static ParsedTip* gParsedPromos = nullptr;
static int gParsedPromoCount = 0;
static bool gSelectedIsPromo = false;
static int gSelectedTipIdx = -1;

static int ParseTipsFromString(const char* src, const char* prefix, ParsedTip*& outTips) {
    StrVec lines;
    Split(&lines, src, "\n");
    int n = 0;
    for (int i = 0; i < lines.Size(); i++) {
        const char* line = lines.At(i);
        if (!str::IsEmptyOrWhiteSpace(line)) {
            n++;
        }
    }
    if (n == 0) {
        return 0;
    }
    outTips = new ParsedTip[n];
    int count = 0;
    for (int i = 0; i < lines.Size(); i++) {
        const char* line = lines.At(i);
        if (str::IsEmptyOrWhiteSpace(line)) {
            continue;
        }
        if (prefix) {
            TempStr prefixed = str::FormatTemp("%s%s", prefix, line);
            ParseTip(outTips[count], prefixed);
        } else {
            ParseTip(outTips[count], line);
        }
        count++;
    }
    return count;
}

static void PickRandomTipOrPromo() {
    bool pickPromo = (gParsedPromoCount > 0) && (rand() % 100 < 30);
    if (pickPromo) {
        gSelectedIsPromo = true;
        gSelectedTipIdx = rand() % gParsedPromoCount;
    } else if (gParsedTipCount > 0) {
        gSelectedIsPromo = false;
        gSelectedTipIdx = rand() % gParsedTipCount;
    }
}

static void EnsureTipsParsed() {
    if (gParsedTips || gParsedPromos) {
        return;
    }
    gParsedTipCount = ParseTipsFromString(sumatraTips, "Tip: ", gParsedTips);
    gParsedPromoCount = ParseTipsFromString(sumatraPromos, nullptr, gParsedPromos);
    PickRandomTipOrPromo();
}

static void PickAnotherRandomTip() {
    bool prevIsPromo = gSelectedIsPromo;
    int prev = gSelectedTipIdx;
    // keep picking until we get a different one
    int maxIter = 100;
    while (maxIter-- > 0) {
        PickRandomTipOrPromo();
        if (gSelectedIsPromo != prevIsPromo || gSelectedTipIdx != prev) {
            return;
        }
    }
}

constexpr COLORREF kAboutBorderCol = RGB(0, 0, 0);

constexpr int kAboutLeftRightSpaceDx = 8;
constexpr int kAboutMarginDx = 10;
constexpr int kAboutBoxMarginDy = 6;
constexpr int kAboutTxtDy = 6;
constexpr int kAboutRectPadding = 8;

constexpr int kInnerPadding = 8;

constexpr const char* kSumatraTxtFont = "Arial Black";
constexpr int kSumatraTxtFontSize = 24;

constexpr const char* kVersionTxtFont = "Arial Black";
constexpr int kVersionTxtFontSize = 12;

#define LAYOUT_LTR 0

static ATOM gAtomAbout;
static HWND gHwndAbout;
static Tooltip* gAboutTooltip = nullptr;
static const char* gClickedURL = nullptr;

struct AboutLayoutInfoEl {
    /* static data, must be provided */
    const char* leftTxt;
    const char* rightTxt;
    const char* url;

    /* data calculated by the layout */
    Rect leftPos;
    Rect rightPos;
};

static AboutLayoutInfoEl gAboutLayoutInfo[] = {
    {"website", "SumatraPDF website", kWebsiteURL},
    {"manual", "SumatraPDF manual", kManualURL},
    {"forums", "SumatraPDF forums", "https://github.com/sumatrapdfreader/sumatrapdf/discussions"},
    {"programming", "The Programmers", "https://github.com/sumatrapdfreader/sumatrapdf/blob/master/AUTHORS"},
    {"licenses", "Various Open Source", "https://github.com/sumatrapdfreader/sumatrapdf/blob/master/AUTHORS"},
#if defined(GIT_COMMIT_ID_STR)
    {"last change", "git commit " GIT_COMMIT_ID_STR,
     "https://github.com/sumatrapdfreader/sumatrapdf/commit/" GIT_COMMIT_ID_STR},
#endif
#if defined(PRE_RELEASE_VER)
    {"a note", "Pre-release version, for testing only!", nullptr},
#endif
#ifdef DEBUG
    {"a note", "Debug version, for testing only!", nullptr},
#endif
    {nullptr, nullptr, nullptr}};

static Vec<StaticLink*> gStaticLinks;

void SetPromoString(const char* s) {
    if (!s) return;
    str::ReplaceWithCopy(&promoFromServer, s);
}

static TempStr GetAppVersionTemp() {
    TempStr s = str::DupTemp("v" CURR_VERSION_STRA);
    if (IsProcess64()) {
        s = str::JoinTemp(s, " 64-bit");
    } else {
        s = str::JoinTemp(s, " 32-bit");
    }
    if (gIsDebugBuild) {
        s = str::JoinTemp(s, " (dbg)");
    }
    return s;
}

constexpr COLORREF kCol1 = RGB(196, 64, 50);
constexpr COLORREF kCol2 = RGB(227, 107, 35);
constexpr COLORREF kCol3 = RGB(93, 160, 40);
constexpr COLORREF kCol4 = RGB(69, 132, 190);
constexpr COLORREF kCol5 = RGB(112, 115, 207);

static void DrawSumatraVersion(HDC hdc, Rect rect) {
    uint fmt = DT_LEFT | DT_NOCLIP;
    HFONT fontSumatraTxt = CreateSimpleFont(hdc, kSumatraTxtFont, kSumatraTxtFontSize);
    HFONT fontVersionTxt = CreateSimpleFont(hdc, kVersionTxtFont, kVersionTxtFontSize);

    SetBkMode(hdc, TRANSPARENT);

    const char* txt = kAppName;
    Size txtSize = HdcMeasureText(hdc, txt, fmt, fontSumatraTxt);
    Rect mainRect(rect.x + (rect.dx - txtSize.dx) / 2, rect.y + (rect.dy - txtSize.dy) / 2, txtSize.dx, txtSize.dy);

    // draw SumatraPDF in colorful way
    Point pt = mainRect.TL();
    // colorful version
    static COLORREF cols[] = {kCol1, kCol2, kCol3, kCol4, kCol5, kCol5, kCol4, kCol3, kCol2, kCol1};
    char buf[2] = {};
    for (int i = 0; i < str::Leni(kAppName); i++) {
        SetTextColor(hdc, cols[i % dimofi(cols)]);
        buf[0] = kAppName[i];
        HdcDrawText(hdc, buf, pt, fmt, fontSumatraTxt);
        txtSize = HdcMeasureText(hdc, buf, fmt, fontSumatraTxt);
        pt.x += txtSize.dx;
    }

    SetTextColor(hdc, ThemeWindowTextColor());
    int x = mainRect.x + mainRect.dx + DpiScale(hdc, kInnerPadding);
    int y = mainRect.y;

    TempStr ver = GetAppVersionTemp();
    Point p = {x, y};
    HdcDrawText(hdc, ver, p, fmt, fontVersionTxt);
    p.y += DpiScale(hdc, 13);
    if (gIsPreReleaseBuild) {
        HdcDrawText(hdc, "Pre-release", p, fmt);
    }
}

// draw on the bottom right
static Rect DrawHideFrequentlyReadLink(HWND hwnd, HDC hdc, const char* txt) {
    HFONT fontLeftTxt = CreateSimpleFont(hdc, "MS Shell Dlg", 16);

    VirtWndText w(hwnd, txt, fontLeftTxt);
    w.isRtl = IsUIRtl();
    w.withUnderline = true;
    Size txtSize = w.GetIdealSize(true);

    auto col = ThemeWindowLinkColor();
    ScopedSelectObject pen(hdc, CreatePen(PS_SOLID, 1, col), true);

    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);
    Rect rc = ClientRect(hwnd);

    int innerPadding = DpiScale(hwnd, kInnerPadding);
    Rect r = {0, 0, txtSize.dx, txtSize.dy};
    PositionRB(rc, r);
    MoveXY(r, -innerPadding, -innerPadding);
    w.SetBounds(r);
    w.Paint(hdc);

    // make the click target larger
    r.Inflate(innerPadding, innerPadding);
    return r;
}

static Size CalcSumatraVersionSize(HDC hdc) {
    HFONT fontSumatraTxt = CreateSimpleFont(hdc, kSumatraTxtFont, kSumatraTxtFontSize);
    HFONT fontVersionTxt = CreateSimpleFont(hdc, kVersionTxtFont, kVersionTxtFontSize);

    /* calculate minimal top box size */
    Size sz = HdcMeasureText(hdc, kAppName, fontSumatraTxt);
    sz.dy = sz.dy + DpiScale(hdc, kAboutBoxMarginDy * 2);

    /* consider version and version-sub strings */
    TempStr ver = GetAppVersionTemp();
    Size txtSize = HdcMeasureText(hdc, ver, fontVersionTxt);
    int minWidth = txtSize.dx + DpiScale(hdc, 8);
    int dx = std::max(txtSize.dx, minWidth);
    sz.dx += 2 * (dx + DpiScale(hdc, kInnerPadding));
    return sz;
}

static TempStr TrimGitTemp(char* s) {
    if (gitCommidId && str::EndsWith(s, gitCommidId)) {
        auto sLen = str::Len(s);
        auto gitLen = str::Len(gitCommidId);
        s = str::DupTemp(s, sLen - gitLen - 7);
    }
    return s;
}

/* Draws the about screen and remembers some state for hyperlinking.
   It transcribes the design I did in graphics software - hopeless
   to understand without seeing the design. */
static void DrawAbout(HWND hwnd, HDC hdc, Rect rect, Vec<StaticLink*>& staticLinks) {
    auto col = ThemeWindowTextColor();
    AutoDeletePen penBorder(CreatePen(PS_SOLID, ABOUT_LINE_OUTER_SIZE, col));
    AutoDeletePen penDivideLine(CreatePen(PS_SOLID, ABOUT_LINE_SEP_SIZE, col));
    col = ThemeWindowLinkColor();
    AutoDeletePen penLinkLine(CreatePen(PS_SOLID, ABOUT_LINE_SEP_SIZE, col));

    HFONT fontLeftTxt = CreateSimpleFont(hdc, kLeftTextFont, kLeftTextFontSize);
    HFONT fontRightTxt = CreateSimpleFont(hdc, kRightTextFont, kRightTextFontSize);

    ScopedSelectObject font(hdc, fontLeftTxt); /* Just to remember the orig font */

    Rect rc = ClientRect(hwnd);
    col = ThemeMainWindowBackgroundColor();
    AutoDeleteBrush brushAboutBg = CreateSolidBrush(col);
    FillRect(hdc, rc, brushAboutBg);

    /* render title */
    Rect titleRect(rect.TL(), CalcSumatraVersionSize(hdc));

    ScopedSelectObject brush(hdc, CreateSolidBrush(col), true);
    ScopedSelectObject pen(hdc, penBorder);
#ifndef ABOUT_USE_LESS_COLORS
    Rectangle(hdc, rect.x, rect.y + ABOUT_LINE_OUTER_SIZE, rect.x + rect.dx,
              rect.y + titleRect.dy + ABOUT_LINE_OUTER_SIZE);
#else
    Rect titleBgBand(0, rect.y, rc.dx, titleRect.dy);
    RECT rcLogoBg = titleBgBand.ToRECT();
    FillRect(hdc, &rcLogoBg, bgBrush);
    DrawLine(hdc, Rect(0, rect.y, rc.dx, 0));
    DrawLine(hdc, Rect(0, rect.y + titleRect.dy, rc.dx, 0));
#endif

    titleRect.Offset((rect.dx - titleRect.dx) / 2, 0);
    DrawSumatraVersion(hdc, titleRect);

    /* render attribution box */
    col = ThemeWindowTextColor();
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);

#ifndef ABOUT_USE_LESS_COLORS
    Rectangle(hdc, rect.x, rect.y + titleRect.dy, rect.x + rect.dx, rect.y + rect.dy);
#endif

    /* render text on the left*/
    SelectObject(hdc, fontLeftTxt);
    uint fmt = DT_LEFT | DT_NOCLIP;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        auto& pos = el->leftPos;
        HdcDrawText(hdc, el->leftTxt, pos, fmt);
    }

    /* render text on the right */
    SelectObject(hdc, fontRightTxt);
    SelectObject(hdc, penLinkLine);
    DeleteVecMembers(staticLinks);
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        bool hasUrl = CanAccessDisk() && el->url;
        if (hasUrl) {
            col = ThemeWindowLinkColor();
        } else {
            col = ThemeWindowTextColor();
        }
        SetTextColor(hdc, col);
        char* s = (char*)el->rightTxt;
        s = TrimGitTemp(s);
        auto& pos = el->rightPos;
        HdcDrawText(hdc, s, pos, fmt);

        if (hasUrl) {
            int underlineY = pos.y + pos.dy - 3;
            DrawLine(hdc, Rect(pos.x, underlineY, pos.dx, 0));
            auto sl = new StaticLink(pos, el->url, el->url);
            staticLinks.Append(sl);
        }
    }

    SelectObject(hdc, penDivideLine);
    Rect divideLine(gAboutLayoutInfo[0].rightPos.x - DpiScale(hwnd, kAboutLeftRightSpaceDx), rect.y + titleRect.dy + 4,
                    0, rect.y + rect.dy - 4 - gAboutLayoutInfo[0].rightPos.y);
    DrawLine(hdc, divideLine);
}

static void UpdateAboutLayoutInfo(HWND hwnd, HDC hdc, Rect* rect) {
    HFONT fontLeftTxt = CreateSimpleFont(hdc, kLeftTextFont, kLeftTextFontSize);
    HFONT fontRightTxt = CreateSimpleFont(hdc, kRightTextFont, kRightTextFontSize);

    /* calculate minimal top box size */
    Size headerSize = CalcSumatraVersionSize(hdc);

    /* calculate left text dimensions */
    int leftLargestDx = 0;
    int leftDy = 0;
    uint fmt = DT_LEFT;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        Size txtSize = HdcMeasureText(hdc, el->leftTxt, fmt, fontLeftTxt);
        el->leftPos.dx = txtSize.dx;
        el->leftPos.dy = txtSize.dy;

        if (el == &gAboutLayoutInfo[0]) {
            leftDy = el->leftPos.dy;
        } else {
            ReportIf(leftDy != el->leftPos.dy);
        }
        if (leftLargestDx < el->leftPos.dx) {
            leftLargestDx = el->leftPos.dx;
        }
    }

    /* calculate right text dimensions */
    int rightLargestDx = 0;
    int rightDy = 0;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        char* s = (char*)el->rightTxt;
        s = TrimGitTemp(s);
        Size txtSize = HdcMeasureText(hdc, s, fmt, fontRightTxt);
        el->rightPos.dx = txtSize.dx;
        el->rightPos.dy = txtSize.dy;

        if (el == &gAboutLayoutInfo[0]) {
            rightDy = el->rightPos.dy;
        } else {
            ReportIf(rightDy != el->rightPos.dy);
        }
        if (rightLargestDx < el->rightPos.dx) {
            rightLargestDx = el->rightPos.dx;
        }
    }

    int leftRightSpaceDx = DpiScale(hwnd, kAboutLeftRightSpaceDx);
    int marginDx = DpiScale(hwnd, kAboutMarginDx);
    int aboutTxtDy = DpiScale(hwnd, kAboutTxtDy);
    /* calculate total dimension and position */
    Rect minRect;
    minRect.dx = leftRightSpaceDx + leftLargestDx + ABOUT_LINE_SEP_SIZE + rightLargestDx + leftRightSpaceDx;
    if (minRect.dx < headerSize.dx) {
        minRect.dx = headerSize.dx;
    }
    minRect.dx += 2 * ABOUT_LINE_OUTER_SIZE + 2 * marginDx;

    minRect.dy = headerSize.dy;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        minRect.dy += rightDy + aboutTxtDy;
    }
    minRect.dy += 2 * ABOUT_LINE_OUTER_SIZE + 4;

    Rect rc = ClientRect(hwnd);
    minRect.x = (rc.dx - minRect.dx) / 2;
    minRect.y = (rc.dy - minRect.dy) / 2;

    if (rect) {
        *rect = minRect;
    }

    /* calculate text positions */
    int linePosX = ABOUT_LINE_OUTER_SIZE + marginDx + leftLargestDx + leftRightSpaceDx;
    int currY = minRect.y + headerSize.dy + 4;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        el->leftPos.x = minRect.x + linePosX - leftRightSpaceDx - el->leftPos.dx;
        el->leftPos.y = currY + (rightDy - leftDy) / 2;
        el->rightPos.x = minRect.x + linePosX + leftRightSpaceDx;
        el->rightPos.y = currY;
        currY += rightDy + aboutTxtDy;
    }
}

static void OnPaintAbout(HWND hwnd) {
    PAINTSTRUCT ps;
    Rect rc;
    HDC hdc = BeginPaint(hwnd, &ps);
    SetLayout(hdc, LAYOUT_LTR);
    UpdateAboutLayoutInfo(hwnd, hdc, &rc);
    DrawAbout(hwnd, hdc, rc, gStaticLinks);
    EndPaint(hwnd, &ps);
}

static void OnSizeAbout(HWND hwnd) {
    // TODO: do I need anything here?
}

static void CopyAboutInfoToClipboard() {
    StrBuilder info(512);
    TempStr ver = GetAppVersionTemp();
    info.AppendFmt("%s %s\r\n", kAppName, ver);
    for (int i = info.Size() - 2; i > 0; i--) {
        info.AppendChar('-');
    }
    info.Append("\r\n");
    // concatenate all the information into a single string
    // (cf. CopyPropertiesToClipboard in SumatraProperties.cpp)
    int maxLen = 0;
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        maxLen = std::max(maxLen, str::Leni(el->leftTxt));
    }
    for (AboutLayoutInfoEl* el = gAboutLayoutInfo; el->leftTxt; el++) {
        for (int i = maxLen - str::Leni(el->leftTxt); i > 0; i--) {
            info.AppendChar(' ');
        }
        info.AppendFmt("%s: %s\r\n", el->leftTxt, el->url ? el->url : el->rightTxt);
    }
    CopyTextToClipboard(info.LendData());
}

TempStr GetStaticLinkAtTemp(Vec<StaticLink*>& staticLinks, int x, int y, StaticLink** linkOut) {
    if (!CanAccessDisk()) {
        return nullptr;
    }

    Point pt(x, y);
    for (int i = 0; i < staticLinks.Size(); i++) {
        if (staticLinks.at(i)->rect.Contains(pt)) {
            auto link = staticLinks.At(i);
            if (linkOut) {
                *linkOut = link;
            }
            return str::DupTemp(link->target);
        }
    }

    return nullptr;
}

static void CreateInfotipForLink(StaticLink* linkInfo) {
    if (gAboutTooltip != nullptr) {
        return;
    }

    Tooltip::CreateArgs args;
    args.parent = gHwndAbout;
    args.font = GetAppFont();
    args.isRtl = IsUIRtl();

    gAboutTooltip = new Tooltip();
    gAboutTooltip->Create(args);
    gAboutTooltip->SetSingle(linkInfo->tooltip, linkInfo->rect, false);
}

static void DeleteInfotip() {
    if (gAboutTooltip == nullptr) {
        return;
    }
    // gAboutTooltip->Hide();
    delete gAboutTooltip;
    gAboutTooltip = nullptr;
}

LRESULT CALLBACK WndProcAbout(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const char* url;
    Point pt;

    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    switch (msg) {
        case WM_CREATE:
            ReportIf(gHwndAbout);
            if (UseDarkModeLib()) {
                DarkMode::setDarkTitleBarEx(hwnd, true);
            }
            break;

        case WM_ERASEBKGND:
            // do nothing, helps to avoid flicker
            return TRUE;

        case WM_SIZE:
            OnSizeAbout(hwnd);
            break;

        case WM_PAINT:
            OnPaintAbout(hwnd);
            break;

        case WM_SETCURSOR:
            pt = HwndGetCursorPos(hwnd);
            if (!pt.IsEmpty()) {
                StaticLink* linkInfo;
                if (GetStaticLinkAtTemp(gStaticLinks, pt.x, pt.y, &linkInfo)) {
                    CreateInfotipForLink(linkInfo);
                    SetCursorCached(IDC_HAND);
                    return TRUE;
                }
            }
            DeleteInfotip();
            return DefWindowProc(hwnd, msg, wp, lp);

        case WM_LBUTTONDOWN: {
            url = GetStaticLinkAtTemp(gStaticLinks, x, y, nullptr);
            str::ReplaceWithCopy(&gClickedURL, url);
        } break;

        case WM_LBUTTONUP:
            url = GetStaticLinkAtTemp(gStaticLinks, x, y, nullptr);
            if (url && str::Eq(url, gClickedURL)) {
                SumatraLaunchBrowser(url);
            }
            break;

        case WM_CHAR:
            if (VK_ESCAPE == wp) {
                DestroyWindow(hwnd);
            }
            break;

        case WM_COMMAND:
            if (CmdCopySelection == LOWORD(wp)) {
                CopyAboutInfoToClipboard();
            }
            break;

        case WM_DESTROY:
            DeleteInfotip();
            ReportIf(!gHwndAbout);
            gHwndAbout = nullptr;
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

constexpr const WCHAR* kAboutClassName = L"SUMATRA_PDF_ABOUT";

void ShowAboutWindow(MainWindow* win) {
    if (gHwndAbout) {
        SetActiveWindow(gHwndAbout);
        return;
    }

    if (!gAtomAbout) {
        WNDCLASSEX wcex;
        FillWndClassEx(wcex, kAboutClassName, WndProcAbout);
        HMODULE h = GetModuleHandleW(nullptr);
        wcex.hIcon = LoadIcon(h, MAKEINTRESOURCE(GetAppIconID()));
        gAtomAbout = RegisterClassEx(&wcex);
        ReportIf(!gAtomAbout);
    }

    TempWStr title = ToWStrTemp(_TRA("About SumatraPDF"));
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int dx = CW_USEDEFAULT;
    int dy = CW_USEDEFAULT;
    HINSTANCE h = GetModuleHandleW(nullptr);
    gHwndAbout = CreateWindowExW(0, kAboutClassName, title, style, x, y, dx, dy, nullptr, nullptr, h, nullptr);
    if (!gHwndAbout) {
        return;
    }

    HwndSetRtl(gHwndAbout, IsUIRtl());

    // get the dimensions required for the about box's content
    Rect rc;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(gHwndAbout, &ps);
    SetLayout(hdc, LAYOUT_LTR);
    UpdateAboutLayoutInfo(gHwndAbout, hdc, &rc);
    EndPaint(gHwndAbout, &ps);
    int rectPadding = DpiScale(gHwndAbout, kAboutRectPadding);
    rc.Inflate(rectPadding, rectPadding);

    // resize the new window to just match these dimensions
    Rect wRc = WindowRect(gHwndAbout);
    Rect cRc = ClientRect(gHwndAbout);
    wRc.dx += rc.dx - cRc.dx;
    wRc.dy += rc.dy - cRc.dy;
    MoveWindow(gHwndAbout, wRc.x, wRc.y, wRc.dx, wRc.dy, FALSE);

    HwndPositionInCenterOf(gHwndAbout, win->hwndFrame);
    ShowWindow(gHwndAbout, SW_SHOW);
}

void DrawAboutPage(MainWindow* win, HDC hdc) {
    Rect rc = ClientRect(win->hwndCanvas);
    UpdateAboutLayoutInfo(win->hwndCanvas, hdc, &rc);
    DrawAbout(win->hwndCanvas, hdc, rc, win->staticLinks);
    if (HasPermission(Perm::SavePreferences | Perm::DiskAccess) && SettingsRememberOpenedFiles()) {
        Rect rect = DrawHideFrequentlyReadLink(win->hwndCanvas, hdc, _TRA("Show frequently read"));
        auto sl = new StaticLink(rect, kLinkShowList);
        win->staticLinks.Append(sl);
    }
}

/* alternate static page to display when no document is loaded */

constexpr int kThumbsSeparatorDy = 2;
constexpr int kThumbsBorderDx = 1;
#define kThumbsMarginLeft DpiScale(hdc, 40)
#define kThumbsMarginRight DpiScale(hdc, 40)
#define kThumbsMarginTop DpiScale(hdc, 50)
#define kThumbsMarginBottom DpiScale(hdc, 40)
#define kThumbsSpaceBetweenX DpiScale(hdc, 38)
#define kThumbsSpaceBetweenY DpiScale(hdc, 58)
#define kThumbsBottomBoxDy DpiScale(hdc, 50)

struct ThumbnailLayout {
    Rect rcPage;
    Size szThumb;
    Rect rcText;
    FileState* fs = nullptr; // info needed to draw the thumbnail
    StaticLink* sl = nullptr;
};

struct FileListRow {
    Rect rcRow;      // 整行区域（用于点击）
    Rect rcPath;     // 第一列：文件路径
    Rect rcPage;     // 第二列：当前页数
    Rect rcPercent;  // 第三列：阅读百分比
    Rect rcFileSize; // 第四列：文件大小
    FileState* fs = nullptr;
    StaticLink* sl = nullptr;
};

struct HomePageLayout {
    // args in
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    Rect rc;
    MainWindow* win = nullptr;

    Rect rcAppWithVer; // SumatraPDF colorful text + version
    Rect rcLine;       // line under bApp
    Rect rcIconOpen;

    HIMAGELIST himlOpen = nullptr;
    VirtWndText* freqRead = nullptr;
    VirtWndText* openDoc = nullptr;
    VirtWndText* hideShowFreqRead = nullptr;
    Vec<ThumbnailLayout> thumbnails; // info for each thumbnail
    Vec<FileListRow> rows;
    // 上传进度区域（仅当 win->uploadProgress != nullptr 时有效）
    Rect rcUploadPanel; // 上传进度区域
    bool hasUpload = false;
    int uploadPanelH = 0;    // 面板高度（用于压缩缩略图区域）
    Rect rcCloseUpload;      // 关闭上传面板按钮
    int totalContentDy = 0;  // total height of all thumbnail rows
    int thumbsVisibleDy = 0; // visible height for thumbnails area
    Rect rcThumbsArea;       // clip rect for thumbnails

    // search filter
    StrVec filterWords;
    Vec<u8> highlighted;
    Rect rcSearchBorder; // border rect drawn around the edit control

    // tip layout
    Rect rcTip;               // background rect for tip area
    ParsedTip* tip = nullptr; // points to gParsedTips or gParsedPromos, not owned

    ~HomePageLayout();
};

HomePageLayout::~HomePageLayout() {
    delete freqRead;
    delete openDoc;
}

constexpr int kOpenDocumentYShift = 7;
constexpr int kThumbsMiddleMargin = 32;
constexpr int kSearchEditDy = 28;
constexpr int kHeaderSearchGapY = 12;
constexpr int kSearchThumbnailsGapY = 12;

static WNDPROC DefWndProcHomeSearch = nullptr;

static LRESULT CALLBACK WndProcHomeSearch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        HwndSetText(hwnd, "");
        MainWindow* win = FindMainWindowByHwnd(GetParent(hwnd));
        if (win) {
            HwndSetFocus(win->hwndCanvas);
            win->RedrawAll(true);
        }
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        HWND parent = GetParent(hwnd);
        return SendMessageW(parent, msg, wp, lp);
    }
    return CallWindowProcW(DefWndProcHomeSearch, hwnd, msg, wp, lp);
}

static void EnsureHomeSearchCreated(MainWindow* win) {
    if (win->hwndHomeSearch) {
        return;
    }
    HMODULE hmod = GetModuleHandleW(nullptr);
    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    DWORD exStyle = 0;
    win->hwndHomeSearch = CreateWindowExW(exStyle, WC_EDITW, L"", style, 0, 0, 100, kSearchEditDy, win->hwndCanvas,
                                          nullptr, hmod, nullptr);
    HDC hdc = GetDC(win->hwndCanvas);
    HFONT font = CreateSimpleFont(hdc, "MS Shell Dlg", 14);
    ReleaseDC(win->hwndCanvas, hdc);
    SetWindowFont(win->hwndHomeSearch, font, TRUE);
    if (!DefWndProcHomeSearch) {
        DefWndProcHomeSearch = (WNDPROC)GetWindowLongPtr(win->hwndHomeSearch, GWLP_WNDPROC);
    }
    SetWindowLongPtr(win->hwndHomeSearch, GWLP_WNDPROC, (LONG_PTR)WndProcHomeSearch);
    Edit_SetCueBannerText(win->hwndHomeSearch, L"search files (Ctrl + F)");
    // add left/right padding so text doesn't overlap the border
    int margin = DpiScale(win->hwndCanvas, 6);
    SendMessage(win->hwndHomeSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
}

void HomePageDestroySearch(MainWindow* win) {
    if (win->hwndHomeSearch) {
        DestroyWindow(win->hwndHomeSearch);
        win->hwndHomeSearch = nullptr;
    }
}

void HomePageFocusSearch(MainWindow* win) {
    EnsureHomeSearchCreated(win);
    ShowWindow(win->hwndHomeSearch, SW_SHOW);
    HwndSetFocus(win->hwndHomeSearch);
}

void PickAnotherRandomPromotion() {
    PickAnotherRandomTip();
}

void LayoutHomePage(HomePageLayout& l) {
    EnsureTipsParsed();

    Vec<FileState*> allFileStates;
    if (gGlobalPrefs->homePageListView && l.win->homePageSelectedFiles.Size() > 0) {
        // 用选择的文件替代历史记录
        StrVec files;
        SliceFirst(l.win->homePageSelectedFiles, files);
        StrVec expandedFiles;
        ExpandFilesAndFolders(files, expandedFiles);
        for (char* path : expandedFiles) {
            // 先从历史记录中查找已有的 FileState
            // FileState* fs = gFileHistory.FindByPath(path);
            // if (!fs) {
            // 没有历史记录，创建临时 FileState（只有路径）
            // fs = New DisplayState(path);  // 或
            FileState* fs = new FileState{}; // fs->filePath = str::Dup(path);
            SetFileStatePath(fs, path);
            // }
            allFileStates.Append(fs);
        }
    } else if (gGlobalPrefs->homePageSortByFrequentlyRead) {
        gFileHistory.GetFrequencyOrder(allFileStates);
    } else {
        gFileHistory.GetRecentlyOpenedOrder(allFileStates);
    }
    auto hwnd = l.hwnd;
    auto hdc = l.hdc;
    auto rc = l.rc;
    auto win = l.win;

    // filter by search query if present
    TempStr searchQuery = nullptr;
    if (win->hwndHomeSearch) {
        searchQuery = HwndGetTextTemp(win->hwndHomeSearch);
    }
    bool hasFilter = searchQuery && searchQuery[0];
    if (hasFilter) {
        SplitFilterToWords(searchQuery, l.filterWords);
    }
    Vec<FileState*> fileStates;
    for (int i = 0; i < allFileStates.Size(); i++) {
        FileState* fs = allFileStates.at(i);
        if (hasFilter) {
            TempStr baseName = path::GetBaseNameTemp(fs->filePath);
            if (!FilterMatches(baseName, l.filterWords)) {
                continue;
            }
        }
        fileStates.Append(fs);
    }

    bool isRtl = IsUIRtl();
    HFONT fontText = CreateSimpleFont(hdc, "MS Shell Dlg", 14);
    HFONT hdrFont = CreateSimpleFont(hdc, "MS Shell Dlg", 24);

    Size sz = CalcSumatraVersionSize(hdc);
    {
        Rect& r = l.rcAppWithVer;
        r.x = rc.dx - sz.dx - 3;
        r.y = 0;
        r.SetSize(sz);
    }

    l.rcLine = {0, sz.dy, rc.dx, 0};

    // --- Pre-compute thumbnail grid x offset so header can align with it ---
    // use unfiltered count so layout stays stable when search filters results
    int nFilesForLayout = allFileStates.Size();
    int colsForLayout =
        (rc.dx - kThumbsMarginLeft - kThumbsMarginRight + kThumbsSpaceBetweenX) / (kThumbnailDx + kThumbsSpaceBetweenX);
    int thumbsColsForLayout = std::max(colsForLayout, 1);
    int thumbsStartX = rc.x + kThumbsMarginLeft +
                       (rc.dx - thumbsColsForLayout * kThumbnailDx - (thumbsColsForLayout - 1) * kThumbsSpaceBetweenX -
                        kThumbsMarginLeft - kThumbsMarginRight) /
                           2;
    if (thumbsStartX < DpiScale(hdc, kInnerPadding)) {
        thumbsStartX = DpiScale(hdc, kInnerPadding);
    } else if (nFilesForLayout == 0) {
        thumbsStartX = kThumbsMarginLeft;
    }

    // --- Step 1: layout header at the top ---
    const char* txt = _TRA("Recently Opened");
    if (gGlobalPrefs->homePageSortByFrequentlyRead) {
        txt = _TRA("Frequently Read");
    }
    VirtWndText* hdr = new VirtWndText(hwnd, txt, hdrFont);
    l.freqRead = hdr;
    hdr->isRtl = isRtl;
    Size txtSize = hdr->GetIdealSize(true);

    int hdrY = DpiScale(hdc, 8);
    Rect rcHdr(thumbsStartX, hdrY, txtSize.dx, txtSize.dy);
    if (isRtl) {
        rcHdr.x = rc.dx - thumbsStartX - rcHdr.dx;
    }
    hdr->SetBounds(rcHdr);

    /* "Open a document" link next to header */
    l.himlOpen = (HIMAGELIST)SendMessageW(win->hwndToolbar, TB_GETIMAGELIST, 0, 0);
    Rect rcIconOpen(0, 0, 0, 0);
    ImageList_GetIconSize(l.himlOpen, &rcIconOpen.dx, &rcIconOpen.dy);

    txt = _TRA("Open a document...");
    auto openDoc = new VirtWndText(hwnd, txt, fontText);
    openDoc->isRtl = isRtl;
    openDoc->withUnderline = true;
    txtSize = openDoc->GetIdealSize(true);

    int openDocSpacing = DpiScale(hdc, 16);
    rcIconOpen.x = rcHdr.x + rcHdr.dx + openDocSpacing;
    rcIconOpen.y = rcHdr.y + rcHdr.dy - rcIconOpen.dy - kOpenDocumentYShift + 3;
    if (isRtl) {
        rcIconOpen.x = rcHdr.x - openDocSpacing - rcIconOpen.dx;
    }
    l.rcIconOpen = rcIconOpen;

    Rect rcOpenDoc(rcIconOpen.x + rcIconOpen.dx + 3, rcHdr.y + rcHdr.dy - txtSize.dy - kOpenDocumentYShift, txtSize.dx,
                   txtSize.dy);
    if (isRtl) {
        rcOpenDoc.x = rcIconOpen.x - rcOpenDoc.dx - 3;
    }
    openDoc->SetBounds(rcOpenDoc);

    rcOpenDoc = rcOpenDoc.Union(rcIconOpen);
    rcOpenDoc.Inflate(10, 10);
    l.openDoc = openDoc;
    auto sl = new StaticLink(rcOpenDoc, kLinkOpenFile);
    win->staticLinks.Append(sl);

    int headerBottomY = rcHdr.y + rcHdr.dy;

    // --- Position search edit below header ---
    EnsureHomeSearchCreated(win);
    int searchEditDy = DpiScale(hdc, kSearchEditDy);
    int headerSearchGap = DpiScale(hdc, kHeaderSearchGapY);
    int searchThumbsGap = DpiScale(hdc, kSearchThumbnailsGapY);
    {
        int thumbsContentWidth = thumbsColsForLayout * kThumbnailDx + (thumbsColsForLayout - 1) * kThumbsSpaceBetweenX;
        int borderDx = thumbsContentWidth * 3 / 4;
        if (borderDx < DpiScale(hdc, 200)) {
            borderDx = DpiScale(hdc, 200);
        }
        int borderX = thumbsStartX + (thumbsContentWidth - borderDx) / 2;
        int borderY = headerBottomY + headerSearchGap;
        int borderDy = searchEditDy + 2; // 1px border on each side
        l.rcSearchBorder = {borderX, borderY, borderDx, borderDy};
        // measure font height so we can vertically center the edit
        HFONT editFont = (HFONT)SendMessage(win->hwndHomeSearch, WM_GETFONT, 0, 0);
        TEXTMETRIC tm;
        HFONT oldFont = (HFONT)SelectObject(hdc, editFont);
        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, oldFont);
        int fontDy = tm.tmHeight + tm.tmExternalLeading + 2; // +2 for caret padding
        int editDy = std::min(fontDy, searchEditDy);
        int editY = borderY + 1 + (searchEditDy - editDy) / 2;
        MoveWindow(win->hwndHomeSearch, borderX + 1, editY, borderDx - 2, editDy, TRUE);
    }
    // border is 1px top + 1px bottom = 2px
    int searchAreaDy = headerSearchGap + searchEditDy + 2 + searchThumbsGap;
    headerBottomY += searchAreaDy;

    // --- Step 2: calculate tip area at the bottom (before thumbnails) ---
    int tipHeight = 0;
    HFONT fontTip = CreateSimpleFont(hdc, "MS Shell Dlg", 16);
    ParsedTip* tip = nullptr;
    if (gGlobalPrefs->showTips && gSelectedTipIdx >= 0) {
        if (gSelectedIsPromo && gSelectedTipIdx < gParsedPromoCount) {
            tip = &gParsedPromos[gSelectedTipIdx];
        } else if (!gSelectedIsPromo && gSelectedTipIdx < gParsedTipCount) {
            tip = &gParsedTips[gSelectedTipIdx];
        }
    }
    if (tip) {
        MeasureTipWords(*tip, hdc, fontTip);
        int tipPadding = DpiScale(hdc, 8);
        // do a preliminary layout to get the height (use thumbnails content width)
        int tipTextWidth = thumbsColsForLayout * kThumbnailDx + (thumbsColsForLayout - 1) * kThumbsSpaceBetweenX;
        LayoutTip(*tip, tipTextWidth, 0, 0);
        tipHeight = tip->totalDy + 2 * tipPadding;
    }

    // step 3 before: upload progress area
    // --- 上传进度面板（如果有上传任务）---
    int uploadPanelH = 0;
    UploadProgress* up = win->uploadProgress;
    if (up) {
        HFONT fontUpload = CreateSimpleFont(hdc, "MS Shell Dlg", 13);
        int rowH = DpiScale(hdc, 20);
        int padding = DpiScale(hdc, 8);
        int nFiles = up->fileStates.Size();
        // 标题行 + 总进度条 + 每个文件一行
        uploadPanelH = padding + rowH + DpiScale(hdc, 6) + rowH + (nFiles * (rowH + DpiScale(hdc, 2))) + padding;
        l.rcUploadPanel = {rc.x + kThumbsMarginLeft, headerBottomY + DpiScale(hdc, 8),
                           rc.dx - kThumbsMarginLeft - kThumbsMarginRight, uploadPanelH};
        l.uploadPanelH = uploadPanelH;
        l.hasUpload = true;

        // 关闭按钮: 右上角20x20
        int btnSz = DpiScale(hdc, 20);
        l.rcCloseUpload = Rect(l.rcUploadPanel.x + l.rcUploadPanel.dx - btnSz - DpiScale(hdc, 4),
                               l.rcUploadPanel.y + DpiScale(hdc, 2), btnSz, btnSz);
        auto csl = new StaticLink(l.rcCloseUpload, kLinkCloseUpload, nullptr);
        win->staticLinks.Append(csl);
    }

    // --- Step 3: middle area for thumbnails ---
    // thumbnails start directly after headerBottomY (which includes kSearchThumbnailsGapY)
    int thumbsTopY = headerBottomY + uploadPanelH + DpiScale(hdc, 8);
    int thumbsBottomY = rc.dy - tipHeight - kThumbsMiddleMargin;
    int thumbsVisibleDy = std::max(0, thumbsBottomY - thumbsTopY);

    l.rcThumbsArea = {0, thumbsTopY, rc.dx, thumbsVisibleDy};

    int nFiles = fileStates.Size();
    int thumbsCols = thumbsColsForLayout;
    int thumbsRows = (nFiles + thumbsCols - 1) / thumbsCols;
    int thumbsContentDy = thumbsRows * (kThumbnailDy + kThumbsSpaceBetweenY) - kThumbsSpaceBetweenY;

    int scrollY = win->homePageScrollY;
    int maxScrollY = std::max(0, thumbsContentDy - thumbsVisibleDy);
    if (scrollY > maxScrollY) {
        scrollY = maxScrollY;
        win->homePageScrollY = scrollY;
    }
    l.totalContentDy = thumbsContentDy;
    l.thumbsVisibleDy = thumbsVisibleDy;

    Point ptOff(thumbsStartX, thumbsTopY - scrollY);

#if 0
// if(false) {
    // 如果有上传任务，在底部预留进度面板
    if (l.win && l.win->uploadProgress) {
        UploadProgress* up = l.win->uploadProgress;
        int panelH = DpiScale(hdc, 24) * (up->fileStates.Size() + 2); // 标题行 + 每文件一行 + 底部间距
        l.rcUploadPanel = {rc.x + kThumbsMarginLeft,
                           rc.y + rc.dy - panelH - DpiScale(hdc, 8),
                           rc.dx - kThumbsMarginLeft - kThumbsMarginRight,
                           panelH};
        l.hasUpload = true;
    }
}
#endif

    if (!gGlobalPrefs->homePageListView) {
        for (int row = 0; row < thumbsRows; row++) {
            for (int col = 0; col < thumbsCols; col++) {
                if (row * thumbsCols + col >= nFiles) {
                    // no more files to display
                    thumbsRows = col > 0 ? row + 1 : row;
                    break;
                }
                int idx = row * thumbsCols + col;
                ThumbnailLayout& thumb = *l.thumbnails.AppendBlanks(1);
                FileState* fs = fileStates.at(row * thumbsCols + col);
                thumb.fs = fs;

                Rect rcPage(ptOff.x + col * (kThumbnailDx + kThumbsSpaceBetweenX),
                            ptOff.y + row * (kThumbnailDy + kThumbsSpaceBetweenY), kThumbnailDx, kThumbnailDy);
                if (isRtl) {
                    rcPage.x = rc.dx - rcPage.x - rcPage.dx;
                }
                RenderedBitmap* thumbImg = LoadThumbnail(fs);
                if (thumbImg) {
                    Size szThumb = thumbImg->GetSize();
                    if (szThumb.dx != kThumbnailDx || szThumb.dy != kThumbnailDy) {
                        rcPage.dy = szThumb.dy * kThumbnailDx / szThumb.dx;
                        rcPage.y += kThumbnailDy - rcPage.dy;
                    }
                    thumb.szThumb = szThumb;
                }
                thumb.rcPage = rcPage;
                int iconSpace = DpiScale(hdc, 20);
                Rect rcText(rcPage.x + iconSpace, rcPage.y + rcPage.dy + 3, rcPage.dx - iconSpace, iconSpace);
                if (isRtl) {
                    rcText.x -= iconSpace;
                }
                thumb.rcText = rcText;
                char* path = fs->filePath;
                Rect slRect = rcText.Union(rcPage).Intersect(l.rcThumbsArea);
                if (!slRect.IsEmpty()) {
                    thumb.sl = new StaticLink(slRect, path, path);
                    win->staticLinks.Append(thumb.sl);
                }
            }
        }

    } else {
        // 固定列宽
        int colGap = DpiScale(hdc, 8);       // 列间距
        int rightMargin = DpiScale(hdc, 80); // 右边距
        int col2Dx = DpiScale(hdc, 100);     // 页数列固定宽度
        int col3Dx = DpiScale(hdc, 80);      // 百分比列固定宽度
        int col4Dx = DpiScale(hdc, 90);      // 文件大小列固定宽度

        int rowH = DpiScale(hdc, 22);
        int rowY = thumbsTopY - scrollY;
        int col1X = thumbsStartX;
        int totalW = rc.dx - col1X - rightMargin; // 总可用宽度
        int col1Dx = totalW - col2Dx - col3Dx - col4Dx - (3 * colGap);
        if (col1Dx < DpiScale(hdc, 100)) {
            col1Dx = DpiScale(hdc, 100); // 防止路径列过窄
        }

        int col2X = col1X + col1Dx + colGap; // 路径列宽度
        int col3X = col2X + col2Dx + colGap; // 页数列宽度
        int col4X = col3X + col3Dx + colGap; // 文件大小列宽度

        for (FileState* fs : fileStates) {
            FileListRow& row = *l.rows.AppendBlanks(1);
            row.fs = fs;
            row.rcPath = {col1X, rowY, col1Dx, rowH};
            row.rcPage = {col2X, rowY, col2Dx, rowH};
            row.rcPercent = {col3X, rowY, col3Dx, rowH};
            row.rcFileSize = {col4X, rowY, col4Dx, rowH};
            row.rcRow = {col1X, rowY, rc.dx - col1X - rightMargin, rowH};
            // StaticLink 绑定文件路径
            row.sl = new StaticLink(row.rcRow, fs->filePath, fs->filePath);
            win->staticLinks.Append(row.sl);
            rowY += rowH + DpiScale(hdc, 2);
        }
    }
    // layout tip at the bottom
    if (tip) {
        Rect rcClient = ClientRect(win->hwndCanvas);
        int tipPadding = DpiScale(hdc, 8);

        int tipY = rcClient.dy - tipHeight;
        // background spans full window width
        l.rcTip = {0, tipY, rcClient.dx, tipHeight};
        l.tip = tip;

        // text area aligned with thumbnails
        int tipTextWidth = thumbsColsForLayout * kThumbnailDx + (thumbsColsForLayout - 1) * kThumbsSpaceBetweenX;
        int tipStartX = thumbsStartX;
        int tipStartY = tipY + tipPadding;
        LayoutTip(*tip, tipTextWidth, tipStartX, tipStartY);

        // register tip links; per-link rects first so they take priority in hit testing
        for (auto& link : tip->links) {
            // compute bounding rect of all words in this link
            Rect linkRect;
            for (int i = link.firstWord; i <= link.lastWord; i++) {
                auto& w = tip->words[i];
                Rect wr = {w.x, w.y, w.dx, w.dy};
                if (i == link.firstWord) {
                    linkRect = wr;
                } else {
                    linkRect = linkRect.Union(wr);
                }
            }
            auto slTip = new StaticLink(linkRect, link.cmd, link.cmd);
            win->staticLinks.Append(slTip);
        }
        // tip background: clicking outside of links picks another tip
        auto slBg = new StaticLink(l.rcTip, kLinkNextTip);
        win->staticLinks.Append(slBg);
    }
}

static void GetFileStateIcon(FileState* fs) {
    if (fs->himl) {
        return;
    }
    SHFILEINFO sfi{};
    sfi.iIcon = -1;
    uint flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    WCHAR* filePathW = ToWStrTemp(fs->filePath);
    fs->himl = (HIMAGELIST)SHGetFileInfoW(filePathW, 0, &sfi, sizeof(sfi), flags);
    fs->iconIdx = sfi.iIcon;
}

static void DrawHomePageLayout(HomePageLayout& l) {
    bool isRtl = IsUIRtl();
    auto hdc = l.hdc;
    auto win = l.win;
    auto textColor = ThemeWindowTextColor();
    auto backgroundColor = ThemeMainWindowBackgroundColor();

    {
        Rect rc = ClientRect(win->hwndCanvas);
        auto color = ThemeMainWindowBackgroundColor();
        FillRect(hdc, rc, color);
    }

    // draw search edit border and background on the canvas
    {
        COLORREF bgCol = ThemeControlBackgroundColor();
        const Rect& sb = l.rcSearchBorder;
        RECT rcBorder = {sb.x, sb.y, sb.x + sb.dx, sb.y + sb.dy};
        // fill interior with control background so padding matches the edit
        HBRUSH brBg = CreateSolidBrush(bgCol);
        FillRect(hdc, &rcBorder, brBg);
        DeleteObject(brBg);
        // draw border frame
        COLORREF borderCol = AccentColor(bgCol, 40);
        HBRUSH brBorder = CreateSolidBrush(borderCol);
        FrameRect(hdc, &rcBorder, brBorder);
        DeleteObject(brBorder);
    }

    if (false) {
        const Rect& r = l.rcAppWithVer;
        DrawSumatraVersion(hdc, r);
    }

    auto color = ThemeWindowTextColor();
    if (false) {
        ScopedSelectObject pen(hdc, CreatePen(PS_SOLID, 1, color), true);
        DrawLine(hdc, l.rcLine);
    }
    HFONT fontText = CreateSimpleFont(hdc, "MS Shell Dlg", 14);

    AutoDeletePen penThumbBorder(CreatePen(PS_SOLID, kThumbsBorderDx, color));
    color = ThemeWindowLinkColor();
    AutoDeletePen penLinkLine(CreatePen(PS_SOLID, 1, color));

    SelectObject(hdc, penThumbBorder);
    SetBkMode(hdc, TRANSPARENT);
    color = ThemeWindowTextColor();
    SetTextColor(hdc, color);

    l.freqRead->Paint(hdc);
    SelectObject(hdc, GetStockBrush(NULL_BRUSH));

    // --- 绘制上传进度面板 ---
    UploadProgress* up = l.win->uploadProgress;
    if (up && l.uploadPanelH > 0) {
        HFONT fontUp = CreateSimpleFont(hdc, "MS Shell Dlg", 13);
        int rowH = DpiScale(hdc, 20);
        int padding = DpiScale(hdc, 8);
        int barH = DpiScale(hdc, 8);
        int colGap = DpiScale(hdc, 8);
        int rightM = DpiScale(hdc, 20);
        RECT rcPanl = ToRECT(l.rcUploadPanel);

        // 面板背景
        // COLORREF panelBg = AccentColor(ThemeControlBackgroundColor(), 15);
        COLORREF panelBg = ThemeControlBackgroundColor();
        HBRUSH brBg = CreateSolidBrush(panelBg);
        // FillRect(hdc, l.rcUploadPanel, panelBg);
        FillRect(hdc, l.rcUploadPanel, brBg);
        DeleteObject(brBg);

        // 画边框
        HPEN penBorder = CreatePen(PS_SOLID, 1, ThemeWindowTextColor());
        ScopedSelectObject selPen(hdc, penBorder, true);
        FrameRect(hdc, &rcPanl, (HBRUSH)GetStockObject(NULL_BRUSH));

        SelectObject(hdc, fontUp);
        SetTextColor(hdc, ThemeWindowTextColor());
        SetBkMode(hdc, TRANSPARENT);

        int x = l.rcUploadPanel.x + padding;
        int y = l.rcUploadPanel.y + padding;
        int panelW = l.rcUploadPanel.dx - 3 * padding;

        // SetTextColor(hdc, ThemeWindowTextColor());
        // SelectObject(hdc, fontUp);

        // --- 总进度标题行 ---
        int nTotal = up->nTotal;
        int nCompleted = AtomicIntGet(&up->nCompleted);
        i64 totalBytes = up->totalBytes;
        i64 uploadedBytes = up->uploadedBytes.Load();
        int globalPct = (totalBytes > 0) ? (int)(uploadedBytes * 100 / totalBytes) : 0;

        TempStr titleStr =
            str::FormatTemp("Uploading: %d / %d files  (%d%%, %s / %s)", nCompleted, nTotal, globalPct,
                            FormatSizeShortTransTemp(uploadedBytes), FormatSizeShortTransTemp(totalBytes));
        Rect rcTitle = {x, y, panelW - DpiScale(hdc, 60), rowH};
        HdcDrawText(hdc, titleStr, rcTitle, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX, fontUp);
        // 关闭按钮 ×
        HFONT fontBtn = CreateSimpleFont(hdc, "MS Shell Dlg", 14);
        SelectObject(hdc, fontBtn);
        RECT rcBtn = ToRECT(l.rcCloseUpload);
        DrawTextW(hdc, L"\u00D7", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // DeleteObject(fontBtn);

        y += rowH + DpiScale(hdc, 4);

        // --- 总进度条 ---
        Rect rcBarBg = {x, y, panelW, barH};
        FillRect(hdc, rcBarBg, ThemeControlBackgroundColor());
        if (globalPct > 0) {
            Rect rcBarFg = {x, y, panelW * globalPct / 100, barH};
            FillRect(hdc, rcBarFg, RGB(0, 120, 215)); // Windows 蓝
        }
        y += barH + DpiScale(hdc, 6);

        // --- 每个文件的进度行 ---
        int pctW = DpiScale(hdc, 80);                              // 百分比列
        int prgW = DpiScale(hdc, 100);                             // 进度条列
        int sizeW = DpiScale(hdc, 200);                            // 已上传/总大小列
        int fileNameW = panelW - prgW - pctW - sizeW - 3 * colGap; // 文件名列
        int col1X = x;
        int col2X = col1X + fileNameW + colGap;
        int col3X = col2X + prgW + colGap;
        int col4X = col3X + pctW + colGap;

        for (int i = 0; i < up->fileStates.Size(); i++) {
            const FileUploadState* fs = up->fileStates[i];
            if (!fs->isActive && !fs->isDone) continue;

            // 文件名（取 basename）
            TempStr baseName = path::GetBaseNameTemp(fs->filePath);
            Rect rcName = {x, y, fileNameW, rowH};
            HdcDrawText(hdc, baseName, rcName, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX, fontUp);

            // 第二列：进度条
            Rect rcBar = {col2X, y + DpiScale(hdc, 4), prgW, rowH - DpiScale(hdc, 8)};
            COLORREF barBg = AccentColor(panelBg, 40);
            COLORREF barFg = ThemeWindowLinkColor();
            FillRect(hdc, rcBar, barBg);
            if (fs->totalBytes > 0) {
                int fillW = (int)(rcBar.dx * fs->uploadedBytes.Load() / fs->totalBytes);
                Rect rcFill = {rcBar.x, rcBar.y, fillW, rcBar.dy};
                FillRect(hdc, rcFill, barFg);
            }

            // 百分比
            int filePct = (fs->totalBytes > 0) ? (int)(fs->uploadedBytes.Load() * 100 / fs->totalBytes) : 0;
            TempStr pctStr = fs->isDone ? (fs->isFailed ? str::DupTemp("❌FAIL") : str::DupTemp("✅ 完成"))
                                        : str::FormatTemp("⏳(%d%%)", filePct);
            Rect rcPct = {col3X, y, pctW, rowH};
            HdcDrawText(hdc, pctStr, rcPct, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX, fontUp);

            // 已上传 / 总大小
            TempStr sizeStr = str::FormatTemp("%s / %s", FormatSizeShortTransTemp(fs->uploadedBytes.Load()),
                                              FormatSizeShortTransTemp(fs->totalBytes));
            Rect rcSize = {col4X, y, sizeW, rowH};
            HdcDrawText(hdc, sizeStr, rcSize, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX, fontUp);

            y += rowH + DpiScale(hdc, 2);
        }

        // DeleteObject(fontUp);
    }

    // clip thumbnails to the middle area
    {
        const Rect& ta = l.rcThumbsArea;
        HRGN thumbsClip = CreateRectRgn(ta.x, ta.y, ta.x + ta.dx, ta.y + ta.dy);
        SelectClipRgn(hdc, thumbsClip);
        DeleteObject(thumbsClip);
    }

    if (!gGlobalPrefs->homePageListView) {
        for (const ThumbnailLayout& thumb : l.thumbnails) {
            FileState* fs = thumb.fs;
            const Rect& page = thumb.rcPage;

            RenderedBitmap* thumbImg = LoadThumbnail(fs);
            if (thumbImg) {
                int savedDC = SaveDC(hdc);
                HRGN clip = CreateRoundRectRgn(page.x, page.y, page.x + page.dx, page.y + page.dy, 10, 10);
                ExtSelectClipRgn(hdc, clip, RGN_AND);
                // note: we used to invert bitmaps in dark theme but that doesn't
                // make sense for thumbnails
                thumbImg->Blit(hdc, page);
                RestoreDC(hdc, savedDC);
                DeleteObject(clip);
            }
            RoundRect(hdc, page.x, page.y, page.x + page.dx, page.y + page.dy, 10, 10);

            const Rect& rect = thumb.rcText;
            char* path = fs->filePath;
            TempStr fileName = path::GetBaseNameTemp(path);
            UINT fmt = DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | (isRtl ? DT_RIGHT : DT_LEFT);

            SelectObject(hdc, fontText);
            {
                RECT rcText = {rect.x, rect.y, rect.x + rect.dx, rect.y + rect.dy};
                DrawMaybeHighlightedTextArgs hlArgs(l.filterWords, l.highlighted);
                hlArgs.hdc = hdc;
                hlArgs.rc = rcText;
                hlArgs.text = fileName;
                hlArgs.colBg = backgroundColor;
                hlArgs.isRtl = isRtl;
                hlArgs.drawFmt = fmt;
                DrawMaybeHighlightedText(hlArgs);
            }

            GetFileStateIcon(fs);
            int x = isRtl ? page.x + page.dx - DpiScale(hdc, 16) : page.x;
            ImageList_Draw(fs->himl, fs->iconIdx, hdc, x, rect.y, ILD_TRANSPARENT);
        }
    } else {
#if 0
        if (win->homePageSelectedFiles.Size() > 0) {
            UINT rfmt = DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX;
            for (const FileListRow& row : l.rows) {
                FileState* fs = row.fs;
                // 第一列：完整文件路径（带省略号）
                char* ip = gGlobalPrefs->remoteIp;
                // TempStr fpath = str::FormatTemp("%s(ip:%s)", fs->filePath, ip);
                TempStr fpath = str::FormatTemp("%s", fs->filePath);
                HdcDrawText(hdc, fpath, row.rcPath, rfmt | DT_LEFT, fontText);
                // 第二列：当前页 / 总页数
                // TempStr pageStr = str::FormatTemp("%d / %d", fs->pageNo, fs->totalPages);
                // HdcDrawText(hdc, pageStr, row.rcPage, rfmt | DT_RIGHT, fontText);
                // 第三列：阅读百分比
                i64 sz = file::GetSize(fs->filePath);
                TempStr szStr = (sz >= 0) ? FormatSizeShortTransTemp(sz) : str::DupTemp("—");
                TempStr pageStr = str::FormatTemp("%d / %s", fs->pageNo, szStr);
                HdcDrawText(hdc, pageStr, row.rcPage, rfmt | DT_RIGHT, fontText);

                // int pct = (fs->totalPages > 0) ? (fs->pageNo * 100 / fs->totalPages) : 0;
                int pct = (sz > 0) ? (fs->pageNo * 100 / sz) : 0;
                TempStr pctStr = str::FormatTemp("%d%%", pct);
                HdcDrawText(hdc, pctStr, row.rcPercent, rfmt | DT_RIGHT, fontText);
                // 第四列：文件大小
                // i64 sz = file::GetSize(fs->filePath);
                // TempStr szStr = (sz >= 0) ? FormatSizeShortTransTemp(sz) : str::DupTemp("—");
                // HdcDrawText(hdc, szStr, row.rcFileSize, rfmt | DT_RIGHT, fontText);
                TempStr finishStr = (fs->pageNo == sz) ? str::DupTemp("✅ 完成") : str::DupTemp("⏳ 传输中");
                HdcDrawText(hdc, finishStr, row.rcFileSize, rfmt | DT_RIGHT, fontText);
            }
        } else {
#endif
        UINT rfmt = DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX;
        for (const FileListRow& row : l.rows) {
            FileState* fs = row.fs;
            // 第一列：完整文件路径（带省略号）
            TempStr fpath = str::FormatTemp("%s", fs->filePath);
            HdcDrawText(hdc, fpath, row.rcPath, rfmt | DT_LEFT, fontText);
            // 第二列：当前页 / 总页数
            TempStr pageStr = str::FormatTemp("%d / %d", fs->pageNo, fs->totalPages);
            HdcDrawText(hdc, pageStr, row.rcPage, rfmt | DT_RIGHT, fontText);
            // 第三列：阅读百分比
            // i64 sz = file::GetSize(fs->filePath);
            // TempStr szStr = (sz >= 0) ? FormatSizeShortTransTemp(sz) : str::DupTemp("—");
            // TempStr pageStr = str::FormatTemp("%d / %s", fs->pageNo, szStr);
            // HdcDrawText(hdc, pageStr, row.rcPage, rfmt | DT_RIGHT, fontText);

            int pct = (fs->totalPages > 0) ? (fs->pageNo * 100 / fs->totalPages) : 0;
            // int pct = (sz > 0) ? (fs->pageNo * 100 / sz) : 0;
            TempStr pctStr = str::FormatTemp("%d%%", pct);
            HdcDrawText(hdc, pctStr, row.rcPercent, rfmt | DT_RIGHT, fontText);
            // 第四列：文件大小
            i64 sz = file::GetSize(fs->filePath);
            TempStr szStr = (sz >= 0) ? FormatSizeShortTransTemp(sz) : str::DupTemp("—");
            HdcDrawText(hdc, szStr, row.rcFileSize, rfmt | DT_RIGHT, fontText);
            // TempStr finishStr = (fs->pageNo == sz) ? str::DupTemp("✅ 完成") : str::DupTemp("⏳ 传输中");
            // HdcDrawText(hdc, finishStr, row.rcFileSize, rfmt | DT_RIGHT, fontText);
        }
        // }
    }

#if 0

    if(false) {
    // 绘制上传进度面板
    if (l.hasUpload && l.win->uploadProgress) {
        UploadProgress* up = l.win->uploadProgress;
        const Rect& rp = l.rcUploadPanel;
        int rowH    = DpiScale(hdc, 22);
        int colGap  = DpiScale(hdc, 8);
        int col2Dx  = DpiScale(hdc, 100); // 进度条列
        int col3Dx  = DpiScale(hdc, 60);  // 百分比列
        int col1Dx  = rp.dx - col2Dx - col3Dx - 2 * colGap;
        int col1X   = rp.x;
        int col2X   = col1X + col1Dx + colGap;
        int col3X   = col2X + col2Dx + colGap;

        // 面板背景
        COLORREF panelBg = ThemeControlBackgroundColor();
        FillRect(hdc, rp, panelBg);

        HFONT fontUp = CreateSimpleFont(hdc, "MS Shell Dlg", 12);
        SelectObject(hdc, fontUp);
        SetTextColor(hdc, ThemeWindowTextColor());
        SetBkMode(hdc, TRANSPARENT);

        // 标题行：总进度
        int y = rp.y + DpiScale(hdc, 4);
        TempStr title = str::FormatTemp("上传进度: %d / %d 文件  %lld / %lld 字节",
            (int)up->nCompleted, up->nTotal,
            up->uploadedBytes.Load(), up->totalBytes);
        Rect rcTitle = {col1X, y, rp.dx, rowH};
        HdcDrawText(hdc, title, rcTitle, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT, fontUp);
        y += rowH + DpiScale(hdc, 2);

        // 每个文件一行
        for (int i = 0; i < up->fileStates.Size(); i++) {
            FileUploadState* fs = up->fileStates[i];

            // 第一列：文件名（带省略号）
            TempStr fname = path::GetBaseNameTemp(fs->filePath);
            Rect rcName = {col1X, y, col1Dx, rowH};
            HdcDrawText(hdc, fname, rcName,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_LEFT, fontUp);

            // 第二列：进度条
            Rect rcBar = {col2X, y + DpiScale(hdc, 4), col2Dx, rowH - DpiScale(hdc, 8)};
            COLORREF barBg  = AccentColor(panelBg, 40);
            COLORREF barFg  = ThemeWindowLinkColor();
            FillRect(hdc, rcBar, barBg);
            if (fs->totalBytes > 0) {
                int fillW = (int)(rcBar.dx * fs->uploadedBytes.Load() / fs->totalBytes);
                Rect rcFill = {rcBar.x, rcBar.y, fillW, rcBar.dy};
                FillRect(hdc, rcFill, barFg);
            }

            // 第三列：百分比或状态
            TempStr pctStr;
            if (fs->isFailed) {
                pctStr = str::DupTemp("失败");
            } else if (fs->isDone) {
                pctStr = str::DupTemp("完成");
            } else if (fs->totalBytes > 0) {
                int pct = (int)(fs->uploadedBytes.Load() * 100 / fs->totalBytes);
                pctStr = str::FormatTemp("%d%%", pct);
            } else {
                pctStr = str::DupTemp("...");
            }
            Rect rcPct = {col3X, y, col3Dx, rowH};
            HdcDrawText(hdc, pctStr, rcPct,
                        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_RIGHT, fontUp);

            y += rowH + DpiScale(hdc, 2);
        }
    }
    }
#endif

    // restore full clip region
    SelectClipRgn(hdc, nullptr);

    color = ThemeWindowLinkColor();
    SetTextColor(hdc, color);
    SelectObject(hdc, penLinkLine);

    int x = l.rcIconOpen.x;
    int y = l.rcIconOpen.y;
    int openIconIdx = 0;
    ImageList_Draw(l.himlOpen, openIconIdx, hdc, x, y, ILD_NORMAL);

    l.openDoc->Paint(hdc);

    if (false) {
        Rect rcFreqRead = DrawHideFrequentlyReadLink(win->hwndCanvas, hdc, _TRA("Hide frequently read"));
        auto sl = new StaticLink(rcFreqRead, kLinkHideList);
        win->staticLinks.Append(sl);
    }

    // draw tip at the bottom
    if (l.tip) {
        COLORREF tipBgCol = ThemeControlBackgroundColor();
        FillRect(hdc, l.rcTip, tipBgCol);

        HFONT fontTip = CreateSimpleFont(hdc, "MS Shell Dlg", 16);
        uint fmt = DT_LEFT | DT_NOCLIP;
        COLORREF textCol = ThemeWindowTextColor();
        COLORREF linkCol = ThemeWindowLinkColor();

        for (auto& w : l.tip->words) {
            Point pt = {w.x, w.y};
            if (w.isLink) {
                SetTextColor(hdc, linkCol);
                HdcDrawText(hdc, w.text, pt, fmt, fontTip);
            } else {
                SetTextColor(hdc, textCol);
                HdcDrawText(hdc, w.text, pt, fmt, fontTip);
            }
        }
        // draw underlines spanning each link
        SelectObject(hdc, penLinkLine);
        for (auto& link : l.tip->links) {
            auto& first = l.tip->words[link.firstWord];
            auto& last = l.tip->words[link.lastWord];
            int underlineY = first.y + first.dy - 3;
            int x1 = first.x;
            int x2 = last.x + last.dx;
            DrawLine(hdc, Rect(x1, underlineY, x2 - x1, 0));
        }
    }
}

void DrawHomePage(MainWindow* win, HDC hdc) {
    HWND hwnd = win->hwndFrame;
    DeleteVecMembers(win->staticLinks);

    HomePageLayout l;
    l.rc = ClientRect(win->hwndCanvas);
    l.hdc = hdc;
    l.hwnd = hwnd;
    l.win = win;
    LayoutHomePage(l);

    DrawHomePageLayout(l);

    // update overlay scrollbar for home page if thumbnails overflow visible area
    bool showScrollbarV = ScrollbarsUseOverlay() && l.totalContentDy > l.thumbsVisibleDy;
    if (showScrollbarV) {
        if (!win->overlayScrollV) {
            win->overlayScrollV =
                OverlayScrollbarCreate(win->hwndCanvas, OverlayScrollbar::Type::Vert, ScrollbarsOverlayMode());
        }
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        si.nMin = 0;
        si.nMax = l.totalContentDy - 1;
        si.nPage = l.thumbsVisibleDy;
        si.nPos = win->homePageScrollY;
        OverlayScrollbarShow(win->overlayScrollV, true);
        OverlayScrollbarSetInfo(win->overlayScrollV, &si, TRUE);
    }
    // show thin scrollbar briefly to indicate content is scrollable
    OverlayScrollbarShow(win->overlayScrollV, showScrollbarV);
}

void HomePageOnVScroll(MainWindow* win, WPARAM wp) {
    USHORT msg = LOWORD(wp);
    HDC hdc = GetDC(win->hwndCanvas);
    int lineDy = kThumbnailDy + kThumbsSpaceBetweenY;
    int pageDy = lineDy * 3;
    ReleaseDC(win->hwndCanvas, hdc);

    int newScrollY = win->homePageScrollY;
    switch (msg) {
        case SB_LINEUP:
            newScrollY -= lineDy;
            break;
        case SB_LINEDOWN:
            newScrollY += lineDy;
            break;
        case SB_PAGEUP:
            newScrollY -= pageDy;
            break;
        case SB_PAGEDOWN:
            newScrollY += pageDy;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            int pos = (int)(short)HIWORD(wp);
            // overlay scrollbar sends full position in HIWORD for THUMBTRACK
            if (win->overlayScrollV) {
                pos = win->overlayScrollV->nTrackPos;
            }
            newScrollY = pos;
            break;
        }
        case SB_TOP:
            newScrollY = 0;
            break;
        case SB_BOTTOM:
            newScrollY = INT_MAX; // will be clamped by layout
            break;
    }
    if (newScrollY < 0) {
        newScrollY = 0;
    }
    if (newScrollY != win->homePageScrollY) {
        win->homePageScrollY = newScrollY;
        InvalidateRect(win->hwndCanvas, nullptr, FALSE);
    }
}

void HomePageOnMouseWheel(MainWindow* win, int delta) {
    Rect rc = ClientRect(win->hwndCanvas);
    HDC hdc = GetDC(win->hwndCanvas);
    int thumbsRowDy = kThumbnailDy + kThumbsSpaceBetweenY;
    ReleaseDC(win->hwndCanvas, hdc);

    int scrollBy = thumbsRowDy / 3;
    if (delta > 0) {
        scrollBy = -scrollBy;
    }
    int newScrollY = win->homePageScrollY + scrollBy;
    if (newScrollY < 0) {
        newScrollY = 0;
    }
    if (newScrollY != win->homePageScrollY) {
        win->homePageScrollY = newScrollY;
        InvalidateRect(win->hwndCanvas, nullptr, FALSE);
    }
}
