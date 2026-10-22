
[SendMe界面设计对话](https://app.devin.ai/search/ng_0cc9d365-8f16-46f6-ae35-4d96a4ad3904)

内容白色面板是用 `Padding` 包着加进 `vbox` 的，灰色 caption 行和白色面板之间的距离，等于**caption 行的下内边距** + **白色面板 `Padding` 的上内边距**之和。想改这段距离，调这两处里的任意一个即可（最直接是改白色面板那层 `Padding` 的 top 值）。

你现在面板那行大概是这样（之前对话里的写法）：

```cpp
// DownloadProgressDialog.cpp, DownloadProgressWnd::Create
vbox->AddChild(new Padding(panel, DpiScaledInsets(kToolDlgPadDx, kToolDlgPadDx)));
```

这里 `DpiScaledInsets(kToolDlgPadDx, kToolDlgPadDx)` 是两参数版（通常是 `vertical, horizontal`），上下用的是同一个 `kToolDlgPadDx`。要单独控制“白区上边到灰区”的距离，改用四参数版显式给 top：

```cpp
// 四参数版，top 单独设成你想要的间距（顺序以项目 DpiScaledInsets 定义为准，top 在首位）
int gapTop = DpiScale(4);   // 想更近就调小，想更远就调大
vbox->AddChild(new Padding(panel, DpiScaledInsets(gapTop, kToolDlgPadDx, kToolDlgPadDx, kToolDlgPadDx)));
```

几个要点：

- `DpiScaledInsets` 的参数顺序以项目实际定义为准：有 `DpiScaledInsets(uniform)`、`DpiScaledInsets(vertical, horizontal)` 和四参数 `(top, right, bottom, left)` 这几种重载。你要改的是 **top**，确认它在首位。 [81-cite-0](#81-cite-0) 
- 如果 caption 行（`capBar`）外层也包了 `Padding` 或设了 `minDy`，它的**底部空白**也会叠加进这段距离——想彻底控制就把 caption 行的下边距设 0，只用面板 `Padding` 的 top 来调。 [81-cite-1](#81-cite-1) 
- 改完布局会在 `Create` 末尾的 `DoLayout` 和后续 `WM_SIZE` 自动重排，不用额外触发。 [81-cite-2](#81-cite-2) 

一句话：把白色面板那层 `Padding` 换成四参数 `DpiScaledInsets`、单独调 top 值，就能改“白区上边 ↔ 灰色标题区”的距离；若 caption 行自带下边距，一并归零以免叠加。`DownloadProgressDialog.cpp` 是你自建文件、不在仓库索引中，以上基于 `Layout.cpp`/`Layout.h` 的 `Padding`/`DpiScaledInsets` 既有定义给出。
