# EX-024 表情资源

本目录保存 `xiaozhi_companion_robot_demo` 的表情视觉审核图、确定性渲染/生产资源生成脚本、正式 manifest 和 80×60 生产预览。八套静态形象及每套 12 帧动态候选已全部通过人工视觉审核；96 帧生产资源已生成，4-bit C catalog 已接入固件并完成 clean build 与烧录，当前核心实屏效果已通过人工确认，剩余完整状态关联和边界回归仍待验证。

## 当前视觉方向

- 原创像素风角色，可为拟动物或史莱姆、龙等虚构生物；参考图只借鉴粗像素颗粒、阶梯块与极简五官的视觉语言，不复制现有游戏角色、标志性造型、原图配色或水印。
- 每套使用独立主题色并共享近黑背景；`icebox` 保留青绿、冰蓝、冷白与蓝灰关系，其余主题色见下方计划。
- 轮廓方正、块状，与整机外形呼应；整体气质偏呆萌、呆懵。
- 以 320x240 横屏的 4:3 可视区域为构图依据，为运行状态、`ROAM ON/OFF` 和网络状态保留角落安全区域。
- 候选图不包含品牌标志、水印或需要固化进屏幕资源的说明文字。

## 目录约定

```text
expressions/
├── README.md
├── expression_manifest.psd1  # 角色与视觉场景的唯一生产资源清单
├── concepts/
│   └── <pack_id>/         # 每套审核图一个独立子目录
└── packs/
    └── <pack_id>/         # 由审核图确定性生成的 80x60 生产预览
```

八套基础 `idle` 形象及全部状态帧均已通过人工审核。小鸭最终 `talk_open` 使用 18×4 主题内色口腔，左右深色边各 1 个逻辑像素且无内部深色横线。首批每套在独立 `concepts/<pack_id>/` 目录包含以下 12 个文件：

1. `idle`：静默待机、睁眼。
2. `blink`：待机眨眼闭合帧。
3. `idle_sway_left_up`、`idle_sway_right_up`：已审核的可选左上、右上倾摆资源；当前空闲运行时不启用，仅保留供后续视觉策略调整。
4. `listen_focus`：聆听或关注声源。
5. `think`：等待 AI 处理。
6. `turn_gaze_left`、`turn_gaze_right`：转向期间的左右视线帧。
7. `talk_closed`、`talk_open`：对话/说话的闭嘴和张嘴帧。
8. `touch_pout_compress`、`touch_pout_expand`：触摸触发的别扭、气鼓鼓两相；必须分别表现脸颊向内挤压和向外鼓起/回弹，嘴形随脸颊联动；不能只变色或只缩放嘴部。

## 本地预览

- `render_system_drawing_preview.ps1` 使用 Windows 本地 `System.Drawing` 生成确定性像素预览，不依赖网络或图片模型。
- `concepts/icebox/icebox_idle_preview_3x.png` 是首张待机表情预览，按 320x240 的 4:3 构图进行 3 倍整数缩放，便于人工查看像素细节。
- `render_theme_baselines.ps1` 生成其余七套基础待机样本及 `concepts/theme_baselines_overview_2x.png` 的 4×2 总览。
- `render_expression_states.ps1` 基于八套已通过的基础预览生成每套 12 张 `<pack_id>_state_<state>_preview_3x.png` 状态帧、每套一张 `<pack_id>_states_overview_1x.png`，以及八套汇总 `concepts/expression_states_overview_2x4.png`；脚本和输出均不依赖网络或图片模型。单套总览按上述状态列表顺序从左到右、从上到下排列。
- `talk_closed` 保留各基础形象的原始嘴形；除已冻结的骨白骷髅外，七套 `talk_open` 使用各自的像素轮廓和主题内口腔色。脚本在输出汇总图前自动检查七个口腔采样点，防止再次退化为统一近黑填充。
- 当前八套活动基础形象为 `icebox` 冰蓝小猫、`crimson_slime` 绯红史莱姆、`jade_frog` 翡翠青蛙、`bone_skull` 骨白骷髅、`cobalt_owl` 钴蓝猫头鹰、`magenta_octopus` 洋红章鱼、`silver_husky` 银灰哈士奇和 `amber_duck` 亮柠檬黄小鸭，均已通过人工静态审核。未入选的机器人、企鹅、小龙、小恶魔和果冻体候选已按人工授权移除，不属于当前素材目录。`concepts/` 审核图和 `System.Drawing` 脚本不是运行时依赖；只有 manifest 中的入选资源经离线生成后进入 C catalog 和 CMake 构建。
- 状态帧用于审核空闲眨眼、可选左上/右上倾摆资源、关注/思考、转向视线、说话开合嘴和触摸脸颊挤压鼓动的视觉可辨性。全部图片状态已通过；当前运行时空闲态只调度 `idle/blink`，不选择两个 sway 帧。脚本逐像素检查小鸭四张眼神帧无原瞳孔残影，并检查张嘴帧具有连续 18x4 的莓红口腔区域、单像素侧边且上喙下方无内部深色横线。

## 生产与扩展

- `generate_expression_assets.ps1` 只处理已通过审核并列入 `expression_manifest.psd1` 的图片，输出 96 张 `packs/<pack_id>/*_80x60.png` 和 `components/companion_expression/generated/companion_expression_assets.c`；构建使用已生成 C 资源，不依赖 `System.Drawing`。
- 当前按 `design.md` 的 80x60、每包最多 16 色、4-bit 调色板索引、编译期 C 资源和 320×240 PSRAM RGB565 Canvas 方案接入；未增加独立图片的产品状态复用 manifest 定义的已审核帧/fallback，并由角落状态标签保留精确语义。
- `Packs` 与 `Scenes` 是独立变长目录。角色变化只改 pack manifest 与对应图片；纯视觉场景变化只改 scene manifest、profile 与对应图片。新增交互触发或生命周期才修改独立表情策略，不修改产品状态机、手势分类或 renderer。
- 当前默认固件已完成 clean build 和 COM7 烧录；用户已确认空闲主体固定/眨眼、触摸核心效果、30px 左右滑动以及当前核心显示效果。其余生产帧的状态关联、转向/说话动画时序、异常恢复和边界回归仍待完整人工验收，本轮未抓运行日志。
