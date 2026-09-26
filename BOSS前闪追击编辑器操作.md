# BOSS 前闪追击：行为树 Task 版本操作步骤

本版由行为树的独立任务发动“前闪 → 刺击”，任务会保持执行状态，直到技能完成或被打断。触发距离在行为树里配置；动画、伤害、位移和冷却仍由 GAS 技能负责。普通攻击和三连击结束后不会自动派生这招。

**受压反击版本的优先级补充：** 接入 [BOSS 受压反击](D:/UE2026/First/BOSS受压反击编辑器操作.md) 后，将原“攻击”分支放在最左，前闪追击放在其右侧、其他移动分支之前；下文早期步骤中的“前闪放最左”仅适用于尚未接入受压反击的版本。已经接好的前闪分支不需要重建。

C++ 已完整编译；2026-09-21 的 18 项自动检查全部通过，包括实际根运动、刺击朝向与横向偏移、任务等待整招结束、移动中抢占、目标变化及中止清理。行为树验证在临时测试树中完成；本文的行为树步骤用于尚未接入新节点的资源，已经完成的步骤无需重复。若只调节本次的刺击方向，直接查看第 5 节及其中的调节文档。下面的行为树、数据资产和动画配置由你在编辑器中完成，使用本次完整编译后的编辑器。

## 1. 确认 BOSS 已拥有追击技能

2026-09-21 已核对你保存的 `DA_BossStartUpData`：数组第 10 项（索引 9）已是 `FirstBossPursuitAbility`，可以直接保留，重点完成第 2–3 节的行为树替换。

1. 在内容浏览器打开 **Content → Enemy → Data → DA_BossStartUpData**。
2. 在细节中搜索 `Reactive`，展开 **Reactive Abilities**。
3. 查看数组里是否已有 **Boss Chase Pursuit**，对应 C++ 类 **FirstBossPursuitAbility**。
4. 已有就保留，不要重复添加。没有时点击数组右侧 **＋**，在新项中搜索并选择该能力。
5. 保存。不要把它添加到 **Activate On Given Abilities**；它需要等行为树任务调用。

`BP_Boss` 的 **Character Start Up Data** 继续使用这份数据资产。无需为本次功能新建 GA 蓝图。

## 2. 移除上一版的追击服务

1. 打开 **Content → Enemy → AI → BT_Boss**。
2. 找到 **战斗** Selector 下的 **靠近** Sequence，其下面已有一个 **Move To**。
3. 如果“靠近”节点上有 **Boss Chase Pursuit** 服务，选中这条服务后删除。新版可能显示为 **Boss Chase Pursuit (Legacy - Remove)**。
4. 只删除该服务，保留“靠近”、它原有的装饰器以及 Move To。
5. 如果别的节点上也添加过这个追击服务，同样删除。

旧服务类暂时保留，方便已保存的资源正常加载；它不再发动技能。安装本版 C++ 后，仅保留旧服务而不添加下文任务，会暂时失去前闪追击。

## 3. 新建明确的“前闪追击”任务分支

最终结构如下。前闪追击与攻击、后退、周旋、靠近处在同一级，并放在“战斗”Selector 的最左边：

```text
战斗（Selector，已有）
├─ 前闪追击（新增 Sequence，放最左）
│  ├─ Boss Can Pursue（装饰器，附着在 Sequence 上）
│  └─ Activate Ability And Wait（任务）
├─ 攻击（已有）
├─ 后退（已有）
├─ 周旋（已有）
└─ 靠近（已有，保留原条件）
   └─ Move To：TargetActor（已有）
```

### 3.1 添加 Sequence

1. 从 **战斗** Selector 节点底部的输出位置拖一条线到空白处。
2. 搜索并选择 **Sequence／序列**。
3. 选中新节点，在细节中的 **Node Name／节点名称** 改为 **前闪追击**。
4. 把它拖到“战斗”所有子节点的最左侧。行为树按从左到右的顺序选择，确认它显示的执行顺序排在原“攻击”之前。
5. 不要把它放到原来的“靠近”里面，也不要复制“靠近”的整组装饰器到这个新节点。

### 3.2 添加发动条件

1. 右键 **前闪追击** Sequence 的主体。
2. 选择 **Add Decorator／添加装饰器**。
3. 搜索 **Boss Can Pursue**，选择它。实际 C++ 类名是 `BTDecorator_BossCanPursue`。
4. 点击新增装饰器，在细节面板确认：

| 字段 | 设置 |
|---|---|
| Ability Tag | `Boss.Ability.Attack.Pursuit` |
| Min Trigger Range | `500` |
| Max Trigger Range | `650` |
| Target Actor Key | `TargetActor` |
| Should Chase Key | `bShouldChase` |
| Player Too Far Key | `bPlayerTooFar` |
| Busy Key | `bIsBusy` |
| Provoked Key | `bProvokedByDamage` |
| Observer Aborts／观察者中止 | **Lower Priority／较低优先级** |
| Inverse Condition／反转条件（若显示） | 不勾选 |

距离单位为厘米，使用角色中心的水平距离。默认值已经填写，黑板键需确认没有变成 None。

装饰器每约 **0.1 秒**重新检查一次条件：正在追击、确实处于远追范围、没有忙碌或处理受袭入战、目标有效、距离合适，并且技能冷却、资源和安全路径允许。条件满足时，它可以中止右侧的普通移动并进入这个任务。

**这里的中止方式必须保持 Lower Priority。不要再额外挂 `bIsBusy = 未设置` 或 `bPlayerTooFar = 已设置` 且设置为 Self／Both 的装饰器。** 它们只能决定是否起手，不能让前闪产生的自身忙碌或距离变化把当前技能中断。

上层原有“战斗”分支的退出条件继续保留：真正失去战斗目标、死亡等情况下可以中止任务并取消技能。

### 3.3 添加执行任务

1. 从 **前闪追击** Sequence 底部拖线。
2. 搜索 **Activate Ability And Wait**，选择该任务。实际 C++ 类名是 `BTTask_ActivateAbilityAndWait`。
3. 选中任务，确认 **Ability Tag = Boss.Ability.Attack.Pursuit**。
4. 确认 **Target Actor Key = TargetActor**。
5. 保存行为树。

不要选择旧的 **Boss Activate Ability By Tag**。旧任务在启动技能后就返回成功，本次新增任务才会等待技能结束。

任务通过标签找到一个已授予的能力，把目标传给它，等待完整的“前闪＋刺击”。目标后退、横移或者前闪缩短了距离，都不会单独取消任务。目标丢失或更换、技能被弹反/破韧中断、行为树真正中止该任务时，会结束这次执行并清理残留。

## 4. 确认普通移动与状态更新

1. 原有 **靠近 → Move To** 继续使用 `TargetActor`，原 **Acceptable Radius = 180** 可保留。
2. 保留“靠近”原有的距离、周旋超时、是否攻击过、忙碌等条件，不需要为本次功能改动它们。
3. 保留原有 **Update Boss State** 与 **Update Combat Distance** 服务。
4. 不要给整个“战斗”或“靠近”分支额外添加 4 秒 Cooldown 装饰器。技能冷却期间，普通跑步仍应继续。

目前 **Update Combat Distance → Strafe Radius = 500**，目标超过该距离时 `bPlayerTooFar` 才为真。因此初测可站在约 **550–600 厘米**。如果以后把最小触发距离改到 500 以下，还要一起考虑远追与周旋的分界。

只有 **Update Boss State** 负责从技能状态更新 `bIsBusy`。新任务和装饰器不会直接写这个键。

## 5. BP_Boss 中的技能配置

打开 **Content → Enemy → BP_Boss → 类默认值**，搜索 `Pursuit`，确认下列参数：

| 字段 | 本轮建议值 | 作用 |
|---|---:|---|
| Enabled | 勾选 | 开启此技能 |
| Cooldown | 4 | 两次启动之间的冷却，单位秒 |
| Forward Dodge Montage | AM_Boss_Dodge_F | 前闪蒙太奇 |
| Followup Attack Montage | AM_Boss_Attack_stinger | 后续刺击蒙太奇 |
| Predicted Landing Attack Range | 250 | 前闪预计落点需要进入的接刀范围，原字段为 DirectAttackRange |
| Attack Warp Range | 450 | 刺击起手时允许吸附的目标中心水平距离 |
| Attack Warp Max Travel Distance | 300 | 刺击吸附目标相对起手位置的最大水平位移，包含前进和横移 |
| Attack Warp Surface Gap | 15 | 刺击预留的表面间距；横移或位移受限时实际间隔可能更大 |
| Attack Aim Yaw Offset | -10 初测 | 刺击朝向补偿，负值向左、正值向右；代码默认 0，换动画需重新校准 |
| Attack Warp Lateral Offset | 0 起步 | 刺击吸附落点的横移，负值向左、正值向右；角度调好后仍偏右再试 -20 |
| Max Dash Distance | 450 | 最大前闪位移 |
| Stop Surface Gap | 80 | 预定落点与玩家胶囊表面的间距 |
| Max Start Facing Angle | 100 | 起手允许的目标偏角 |
| Max Attack Facing Correction | 35 | 前闪后最多修正的出刀角度 |
| Max Target Height Difference | 100 | 允许的双方高度差 |
| Dodge Play Rate | 1.5 | 前闪播放倍率 |
| Use Normal Attack Damage | 勾选 | 复用普通攻击伤害 |
| Use Normal Attack Defense | 勾选 | 复用普通攻击的格挡/弹反规则 |

确认后编译并保存 BP_Boss。动画引用沿用已经制作好的资源，不需要重新重定向。

**Min Trigger Range / Max Trigger Range 已移到行为树的 Boss Can Pursue 装饰器。** 旧 BP 数值仅保留序列化兼容，不再参与判断，也不再作为可编辑字段显示。若此前用过自定义距离，请手动填到新装饰器中。

前闪最大位移 450，加预计接刀范围 250，最多支持从约 700 厘米外进入接刀范围。刺击的吸附范围和推进力度已经独立，不会反过来改变这个前闪起手检查。指定前闪蒙太奇后，旧的 Forward Dodge Animation 和 Dodge Slot Name 备用入口无需调整。

刺击吸附的详细解释、调节步骤和当前动画窗口见 [前闪追击刺击吸附调节](D:/UE2026/First/前闪追击刺击吸附调节.md)。

## 6. 动画通知与资源

前闪保持一个启用根运动的前闪序列，使用 BOSS 骨架、正确的 Slot、单次播放和自动混出。前闪正常结束后技能会自动接刺击，无需另加“触发刺击”通知。

刺击需要有效的武器碰撞窗口。若你已经在蒙太奇或源序列里添加过，就保留，不要重复添加：

1. 打开 **Content → Enemy → AnimBP → Montages → Attacks → AM_Boss_Attack_stinger**。
2. 查看源序列和蒙太奇通知轨道是否已有 **FirstANS_WeaponCollision**。
3. 如果没有，在剑开始有效刺出的帧，右键通知轨道 → **添加通知状态／Add Notify State** → 搜索 `WeaponCollision`。
4. 调整通知状态的起止位置，使其只覆盖有效刺击阶段，在收剑前结束。
5. 保存。具体窗口按动画预览确定，不要覆盖整段准备与收招。

原普通攻击、三连击中的 **Boss.Event.PursuitPoint** 已不再被使用；如曾自行添加，可以只删除这个旧派生通知，保留其他碰撞、音效和根运动通知。

## 7. 运行后如何确认

1. 运行游戏，让 BOSS 正常完成警戒和拔剑。
2. 打开 BT_Boss，在调试对象中选择场景中的 BOSS 实例。
3. 把距离拉到 650 厘米外：应先运行普通 Move To。进入 500–650 的有效远追区间后，应切换到 **前闪追击 → Activate Ability And Wait**。
4. 观察该任务：前闪和刺击期间应持续显示执行中，直到整招结束。前闪使 `bPlayerTooFar` 变假、`bIsBusy` 变真，不应让它提前退出。
5. 在前闪过程中后退或横移：仍会接刺击，允许玩家躲开使其挥空。
6. 在技能冷却期间再次拉开：BOSS 应继续跑步追赶；冷却结束且条件满足时才能再次发动。
7. 弹反刺击、打出破韧或击杀 BOSS：任务应结束，不应稍后补出残留刺击。
8. 目标丢失或更换时：旧任务与旧技能应结束，不应继续追打旧目标。
9. 在墙边、台阶和导航边缘测试：路径安全不合适时不发动前闪，继续原有移动/不可达目标处理。

## 8. 常见问题

| 表现 | 检查位置 |
|---|---|
| 搜索不到新节点 | 确认已完成完整编译并重启 UE；搜索类名 BTDecorator_BossCanPursue、BTTask_ActivateAbilityAndWait |
| 完全不发动 | 是否授予 FirstBossPursuitAbility；两个新节点的 Ability Tag 是否一致；旧服务是否已替换为新分支 |
| 普通移动不切换到技能 | 前闪分支是否位于战斗 Selector 最左侧；装饰器是否 Lower Priority；黑板键是否选对 |
| 起手就停止或前闪后不刺击 | 是否误放在旧靠近分支内；是否额外挂了 Busy/TooFar 的 Self/Both 中止条件；是否真正发生目标丢失、破韧或安全取消 |
| 冷却时停止追赶 | 是否保留原 Move To；是否误把 Cooldown 或技能条件挂到了整个战斗/靠近分支 |
| 动画播放但没有伤害 | 检查第 6 节的武器碰撞窗口和实际命中 |
| 改 BP 旧触发距离无效 | 在 Boss Can Pursue 装饰器里设置 Min/Max Trigger Range |

本次只为追击接入“启动并等待”的任务，其他普通攻击任务暂时保持原流程，避免把无关战斗分支一起改动。
