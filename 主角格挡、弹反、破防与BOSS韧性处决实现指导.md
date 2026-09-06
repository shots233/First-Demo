# 主角格挡、弹反、破防与 BOSS 韧性处决实现指导

> 适用项目：`First`（UE 5.6 / C++ / GAS / Enhanced Input / 中文版编辑器）
>
> 项目核对日期：2026-08-25。本文按当前 `D:\UE2026\First` 的真实源码、行为树和现有动画资产编写。
>
> 前置条件：主角攻击、闪避、精力恢复、BOSS 普通攻击/三连击、BOSS 韧性条、死亡与行为树已经能够运行。
>
> 范围说明：本文按当前单机模式实现；网络预测、多人复制、锁步同步不在本册范围内。
>
> 本文只提供实现指导，不会自动修改 C++、蓝图、行为树、输入或动画资产。
>
> 当前“BOSS 在非攻击状态不播放普通受击”的问题按你的决定暂时保留；本册不会放宽 `GA_Boss_HitReact` 的受击窗口要求。

---

## 0. 先看最终效果

完成本册后，战斗流程是：

```text
按住鼠标右键
→ 主角立刻进入正面格挡
→ 前 0.15 秒同时是弹反窗口
→ 0.15 秒后仍按住，则只保留普通格挡
→ 松开右键，立即失去防御判定，再播放收势动画
→ 持续格挡中按闪避：Dodge 成功 Commit 后立即取消格挡并进入闪避
→ Dodge 因精力或冷却失败：保持原格挡，不会两边都失败

BOSS 普通攻击命中正面格挡
→ 主角不掉血
→ 精力 -25
→ 精力恢复暂停 0.75 秒

BOSS 三连击命中正面格挡
→ 三段分别判定
→ 每挡住一段精力 -15
→ 某一段把精力打到 0：该段仍被挡住，随后主角破防
→ 破防后的后续段数可以正常造成生命伤害

在弹反窗口内接住可弹反攻击
→ 主角不掉血、不消耗格挡精力
→ BOSS 当前整套攻击立即取消
→ BOSS 韧性固定 -50

BOSS 韧性 100 → 50
→ 只播放 1.0 秒普通弹反僵直
→ 不再触发旧版“50% 大僵直”

BOSS 韧性 50 → 0
→ 跳过普通 1 秒僵直
→ 直接进入 5 秒可处决状态
→ 玩家靠近后按 E，双方播放配对处决动画
→ 处决命中帧造成“玩家当前 AttackPower × 10”的原始伤害
→ 若生命归零：沿用现有死亡链并保留处决死亡姿势
→ 若仍有生命：完整播放演出，随后回满韧性并恢复战斗

5 秒内没有处决
→ 移除可处决状态
→ 韧性一次性回满
→ BOSS 恢复行为树

未来投技
→ 明确配置为不可格挡、不可弹反
→ 即使玩家正处于格挡或弹反窗口，投技也会正常命中
```

本册采用以下职责结构：

```text
BOSS 攻击 Ability
├─ 描述本招是否可格挡、可弹反
├─ 描述格挡精力伤害和弹反削韧
├─ 命中时先询问 UDKDefenseComponent
└─ 只有结果为 Damaged 才应用现有生命伤害 GE

UDKDefenseComponent（主角身上、无 Tick）
├─ 判断真实攻击者是否在正面 120° 内
├─ 按优先级返回 Avoided / Parried / Blocked / Damaged
├─ 结算格挡精力与恢复暂停
└─ 发送格挡、破防、弹反事件

GA_DK_GuardParry
├─ 管理按下、弹反窗口、持续格挡和松开
├─ 播放一个带 Section 的完整格挡 Montage
└─ 只管理主角防御生命周期，不计算 BOSS 韧性

GA_Boss_ParryStagger
├─ 接收 Boss.Event.Parried
├─ 取消攻击并关闭武器碰撞
├─ 固定削减 50 点韧性
└─ 韧性未归零时僵直 1 秒，归零时转入 Executable

ABossCharacter + GA_Boss_Executable
├─ 管理最后一次削韧后的 5 秒计时
├─ 管理可处决、超时回韧和行为树停机
└─ 管理 BOSS 侧配对处决、伤害结算及存活/死亡分流
```

---

## 1. 本册采用的最终规则

### 1.1 正式覆盖旧决策

本册规则优先于《BOSS设计决策书》中较早的韧性方案：

| 旧内容 | 本册最终内容 |
|---|---|
| A3：首次降到最大韧性的 50% 触发大僵直 | **取消，不再检测 50% 阈值** |
| A4 / D16：大僵直后韧性不重置 | 因旧大僵直被取消，不再适用 |
| Phase 3 / 8.3 的 50% 大僵直流程 | 改为“普通弹反僵直 → 韧性归零 → 可处决” |
| A5：韧性归零可处决 | 保留 |
| A6：最后一次削韧后 5 秒回满 | 保留，并与可处决窗口统一为 5 秒 |
| A7：玩家普通武器也可削韧 | 仍保留为后续扩展，本册只实现弹反削韧 |
| D18：处决直接击杀 | 改为玩家当前 `AttackPower × 10` 的原始伤害；只有伤害致死时才进入死亡链 |
| D29：普通受击必须处于受击窗口 | 保留；弹反使用独立能力，不复用普通受击能力 |

这里的 `50` 是**固定 50 点韧性**，不是“最大韧性的 50%”。为了得到两次弹反破韧的节奏，首版把 BOSS 的：

```text
Poise = 100
MaxPoise = 100
```

### 1.2 三连击的首版约定

你已经确定“三连击每段可格挡”。本册同时采用下面这个推荐默认值：

```text
三段均可弹反；
任意一段弹反成功后，整套三连立即取消。
```

它不是写死在全局伤害系统里的规则。三连的攻击配置仍可在 `BP_Boss` 中单独关闭 `bParryable`，所以以后如果你想改成“前两段可弹反、最后一段不可弹反”，只需把三段拆成独立攻击描述或让命中事件携带段数，不必重写防御组件。

### 1.3 投技的首版约定

未来投技必须显式使用：

```cpp
bBlockable = false;
bParryable = false;
```

本册的攻击描述默认值本身就是 `false / false`。也就是说，漏配置的新招式默认不可防御；只有明确加入白名单的招式才能被格挡或弹反。这能避免以后新增投技时因为忘记配置而被右键挡住。

### 1.4 可处决与五秒回韧如何共存

统一规则如下：

```text
Poise > 0：最后一次弹反削韧后 5 秒，一次性回满。

Poise == 0：进入 5 秒 Executable 窗口，普通回韧暂停。
             5 秒内处决 → 清除计时器并进入处决。
             5 秒内未处决 → 退出 Executable，韧性回满。
```

这样不会出现“刚进入可处决，下一帧又因为回韧而恢复战斗”的冲突。

---

## 2. 当前项目的真实接入点

### 2.1 现有 BOSS 命中链

当前项目不是在武器碰撞回调中直接扣血，而是：

```text
BossWeapon 产生 Overlap
→ UBossCombatComponent::OnHitTargetActor
→ 向 BOSS 自己发送 DK.Event.MeleeHit
→ GA_Boss_NormalAttack / GA_Boss_ThreeCombo 收到事件
→ MakeBossDamageEffectSpecHandle
→ UFirstGE_Damage
→ UGEExecCalc_DamageTaken
→ UFirstAttributeSet::DamageTaken
→ Health 下降 / Shared.Status.Dead
```

所以防御解析的正确位置是：

```text
HandleMeleeHit 收到目标
→ 先调用 ResolveTargetDefense
→ 结果是 Damaged：继续应用 UFirstGE_Damage
→ 其它结果：本次命中已经被防御系统消费，不应用生命伤害
```

不要把“是否正面、是否格挡、是否可弹反”写进 `UGEExecCalc_DamageTaken`。伤害计算器并不知道当前伤害是剑击、投技、陷阱还是环境伤害，把动作资格塞进去会让所有伤害来源互相耦合。

### 2.2 可以直接复用的内容

| 当前内容 | 本册如何复用 |
|---|---|
| `Stamina / MaxStamina` | 直接作为格挡资源，不新增第二套精力变量 |
| `Poise / MaxPoise` | 直接作为 BOSS 韧性，不重建属性集 |
| 玩家精力 UI、BOSS 韧性 UI | AttributeSet 已广播，不重复创建控件 |
| `DK.Status.Action.Cancelable.By.Parry` | 攻击动画允许弹反取消时，Guard Ability 才能从攻击中启动 |
| `Boss.Status.Staggered` | 普通弹反僵直和可处决阶段统一阻断攻击/移动 |
| `Boss.Ability.Attack` | 弹反时按父标签取消普通攻击和三连击 |
| `FirstANS_WeaponCollision` | 三连的三个独立碰撞窗口继续复用 |
| `AnimNotify_DKGameplayEvent` | 只负责在 BOSS Target Montage 的处决命中帧发送精确 GameplayEvent；结束由玩家 Montage 的 `OnCompleted` 负责 |
| `Shared.Status.Dead` | 处决仍通过 DamageTaken 结算；只有伤害致死时才进入现有死亡链 |
| `BTService_UpdateBossState::ActionBlockingTags` | 继续聚合到一个 `bIsBusy`，不增加一排黑板键 |

### 2.3 本册明确不改的内容

- `GA_Boss_HitReact` 仍要求 `Boss.Status.HitReactWindow`；
- 非攻击状态打 BOSS 不播放普通受击的问题暂时保留；
- 普通玩家轻/重攻击的削韧值尚未确定，本册不擅自加入；
- BOSS 普通攻击 4 秒、三连击 8 秒冷却保持不变；
- BOSS 周旋、后退、靠近的距离逻辑保持不变；
- 主角索敌系统不是格挡正面判断的数据源；即使未锁定，也能正确格挡真实攻击者。

---

## 3. 判定优先级与推荐数值

### 3.1 一次命中的固定优先级

```text
1. 目标无效或已死亡        → Avoided
2. 主角处于无敌帧          → Avoided
3. 攻击者在正面 + 可弹反 + ParryWindow → Parried
4. 攻击者在正面 + 可格挡 + Blocking + 精力 > 0 → Blocked
5. 其它情况                → Damaged
```

弹反必须排在普通格挡前面。因为按下右键的前 0.15 秒同时拥有 `Blocking` 和 `ParryWindow`；如果先判断 Blocking，所有及时防御都会退化为普通格挡。

“导致精力归零的这一击”仍然完整挡住，随后才进入破防。这样三连击的结果更稳定：

```text
第 2 段把精力打到 0 → 第 2 段不扣血，播放破防；
第 3 段如果随后命中 → 因 Blocking 已被移除，正常扣血。
```

### 3.2 推荐初始数值

| 参数 | 首版值 | 说明 |
|---|---:|---|
| 格挡总角度 | 120° | 左右各 60°，点积阈值约 0.5 |
| 弹反窗口 | 0.15 秒 | 从右键按下第一帧开始 |
| 普通攻击格挡精力伤害 | 25 | 不扣生命 |
| 三连每段格挡精力伤害 | 15 | 三段独立结算 |
| 成功弹反精力消耗 | 0 | 首版鼓励精准防御 |
| 普通精力恢复 | 20/秒 | 当前 GE 每 0.5 秒 +10 |
| 格挡期间恢复 | 2/秒 | 新 GE 每 0.5 秒 +1 |
| 受挡后恢复暂停 | 0.75 秒 | 连续格挡会刷新时长 |
| 重新格挡最低精力 | 20 | 防止 0 精力连续抖动格挡 |
| 玩家破防僵直 | 1.3 秒 | 可在防御组件详情中调整 |
| 格挡移动速度 | 0 | 首版没有格挡侧走动画，先禁止滑步 |
| 弹反削减 BOSS 韧性 | 50 点 | 固定点数，不是百分比 |
| 普通弹反僵直 | 1.0 秒 | 仅 Poise > 0 时 |
| BOSS 回韧等待 | 5.0 秒 | 从最后一次削韧起算 |
| 可处决窗口 | 5.0 秒 | 与回韧计时统一 |
| 处决触发距离 | 200 | 玩家按 E 搜索最近可处决 BOSS |

首版将格挡移动速度设成 `0` 是为了避免新的滑步：当前主角可用的是完整上半身/全身格挡姿势，没有配套的格挡前后左右移动 Blend Space。等以后补齐格挡移动动画后，再把速度改成 100～125。

---

## 4. 为什么这套结构不臃肿

### 4.1 行为树不负责微观战斗判定

行为树只需要知道：

```text
BOSS 现在是否忙碌？
```

`Boss.Status.Staggered`、`Boss.Status.Executable`、`Boss.Status.BeingExecuted` 都汇总到现有 `bIsBusy`。行为树不需要出现：

```text
bWasParried
bPoiseIsZero
bExecutionPlaying
bParryStaggerFinished
```

这些属于一次 Ability 的生命周期，放进黑板反而会造成状态重复与清理遗漏。

### 4.2 防御组件不拥有攻击规则

`UDKDefenseComponent` 只回答“按照这份攻击描述，这一击的结果是什么”。它不知道普通攻击、三连、投技的类名。

```text
普通攻击 → 显式传入 可格挡/可弹反/25/50
三连击   → 显式传入 可格挡/可弹反/15/50
未来投技 → 显式传入 false/false/0/0
```

以后添加新招式只添加数据，不在组件中增加 `if (AttackName == ...)`。

### 4.3 普通受击与弹反僵直完全分离

```text
GA_Boss_HitReact
└─ 生命下降触发，并继续要求 HitReactWindow

GA_Boss_ParryStagger
└─ Boss.Event.Parried 触发，不要求 HitReactWindow
```

因此本册不会意外修掉或改变你暂时保留的“非攻击状态普通受击”行为。

---

## 5. 文件与 UE 资产总览

### 5.1 新增 C++ 文件

| 类型或类名 | 父类 | 职责 |
|---|---|---|
| `FirstCombatTypes.h` | 无；这是 `UENUM/USTRUCT` 头文件 | 一次攻击的防御描述与判定结果 |
| `UDKDefenseComponent` | `UActorComponent` | 正面、格挡、弹反、精力和事件解析 |
| `UFirstGE_StaminaChange` | `UGameplayEffect` | SetByCaller 即时修改精力 |
| `UFirstGE_StaminaRegenPause` | `UGameplayEffect` | 0.75 秒恢复暂停，重复命中刷新 |
| `UFirstGE_StaminaRegenGuarding` | `UGameplayEffect` | 格挡期间 2/秒慢恢复 |
| `UFirstGE_PoiseChange` | `UGameplayEffect` | SetByCaller 即时修改 BOSS 韧性 |
| `UFirstGE_ExecutionDamage` | `UGameplayEffect` | 把处决原始伤害写入 DamageTaken |
| `UFirstGA_DKGuardParry` | `UFirstDKGameplayAbility` | 按住格挡、短弹反窗口、Section 表现 |
| `UFirstGA_DKGuardBreak` | `UFirstDKGameplayAbility` | 事件触发的玩家破防僵直 |
| `UFirstGA_DKExecute` | `UFirstDKGameplayAbility` | 玩家侧搜索、对齐和配对处决 |
| `UGA_Boss_ParryStagger` | `UFirstBossGameplayAbility` | BOSS 弹反取消、削韧与 1 秒僵直 |
| `UGA_Boss_Executable` | `UFirstBossGameplayAbility` | 可处决、超时、BOSS 侧配对动画 |

建议保持当前目录命名风格：

```text
Source/First/Public/AbilitySystem/Abilities/DK/
Source/First/Private/AbilitySystem/Abilities/DK/
Source/First/Public/AbilitySystem/Abilities/BOSS/
Source/First/Private/AbilitySystem/Abilities/BOSS/
Source/First/Public/AbilitySystem/GameplayEffects/
Source/First/Private/AbilitySystem/GameplayEffects/
```

#### 5.1.1 初学者：在 UE 5.6 中怎样创建这些类

对于表中有父类的项目，使用：

```text
顶部菜单“工具”
└─ 新建 C++ 类
   └─ 所有类
      └─ 搜索下文写明的父类
```

创建时遵守下面五条：

1. “类名”只填写不带 Unreal 前缀的名字。例如创建 `UFirstGA_DKGuardParry` 时填写 `FirstGA_DKGuardParry`，不要填写开头的 `U`。
2. “类类型（Class Type）”选择 `Public`。本项目的其他类需要包含这些头文件。
3. “路径（Path）”选择下文给出的 `Source/First/Public/...` 目录。UE 会把 `.h` 放进这个 `Public` 目录，并把 `.cpp` 自动放进对应的 `Source/First/Private/...` 目录。
4. 如果第一页没有目标父类，进入“所有类（All Classes）”再搜索。自定义父类 `FirstDKGameplayAbility` 和 `FirstBossGameplayAbility` 尤其需要这样查找。
5. 如果自定义父类仍然搜不到，先关闭编辑器，完整编译 `FirstEditor Win64 Development`，再重新打开编辑器。不要临时改选通用的 `GameplayAbility`，否则会绕过项目父类中已有的公共能力逻辑。

创建五个 GE 类时，父类必须是准确的 `UGameplayEffect`。不要误选名字相近的 `UGameplayEffectExecutionCalculation`；后者是另一种“自定义数值执行计算”类，本册并不使用。

`FirstCombatTypes.h` 是唯一例外：它不是继承类，不使用“新建 C++ 类”向导。用 IDE 新建普通头文件，放到 `Source/First/Public/Types/FirstCombatTypes.h`；本册给出的类型不需要对应的 `.cpp`。

后面的每个新增类章节都会再次写明：**父类、向导类名、Public 路径，以及自动生成的 Private `.cpp` 路径**。如果向导生成了默认示例代码，用对应章节提供的完整代码替换即可。

### 5.2 修改 C++ 文件

| 文件 | 修改内容 |
|---|---|
| `MyGameplayTags.h/.cpp` | 新增输入、能力、状态、事件和 SetByCaller 标签 |
| `FirstAbilitySystemComponent.cpp` | 正确转发 InputReleased 通用事件 |
| `FirstGA_DKDodge.h/.cpp` | 修复双重 `EndAbility`，并在成功 Commit 后取消带权限的 Attack/GuardParry |
| `DKCharacter.h/.cpp` | 创建防御组件，提供格挡/破防/处决 Montage 与处决伤害倍率 |
| `FirstBossGameplayAbility.h/.cpp` | 增加统一防御解析入口 |
| `BossCharacter.h/.cpp` | 攻击防御描述、处决点、动画与五秒回韧 |
| `GA_Boss_NormalAttack.h/.cpp` | 命中前解析防御，取消时兜底关碰撞 |
| `GA_Boss_ThreeCombo.h/.cpp` | 每段命中前解析防御，取消时兜底关碰撞 |
| `GA_Boss_Death.cpp` | 仅当处决伤害致死时，不覆盖配对死亡演出 |
| `FirstGE_StaminaRegen.cpp` | 格挡和暂停期间抑制普通恢复 |
| `BTService_UpdateBossState.cpp` | 新实例默认把僵直/处决视为忙碌 |
| 玩家攻击/闪避/装备 Ability 构造函数 | 攻击/装备禁止在格挡中启动；闪避可在成功提交后取消格挡 |

### 5.3 新增或修改 UE 资产

| 资产 | 操作 |
|---|---|
| `IA_GuardParry` | 新增 Boolean，映射鼠标右键 |
| `IA_Execute` | 新增 Boolean，映射 E |
| `IMC_Default` | 加入两个映射 |
| `DA_InputConfig` | 两个输入加入 Ability Input Actions |
| `BP_DKWeapon_Sword` | 授予 GuardParry 与 Execute Ability |
| `DA_DKStartUpData` | 授予 GuardBreak；应用慢恢复 GE |
| `DA_BossStartUpData` | 授予 ParryStagger 与 Executable |
| `BP_DKCharacter` | 配置防御参数和三个主角 Montage |
| `BP_Boss` | 配置两套攻击描述、弹反/可处决/处决动画和处决点 |
| `GE_Boss_Initialize` | 确认 Poise/MaxPoise = 100/100 |
| `BT_Boss` 的 Update Boss State 服务节点 | 手动补齐阻断 Tag |

`First.Build.cs` 当前已经包含 `GameplayAbilities`、`GameplayTags`、`GameplayTasks`、`AIModule`、`EnhancedInput` 和 `UMG`，本册首版不需要再增加模块。Motion Warping 当前没有启用，本册也不要求启用。

---

## 6. 预检修复：先解决两个现有生命周期问题

这两项不是新增防御功能，但会直接影响“松开右键”和“格挡/闪避互相取消”的测试。建议先修复并单独编译。

### 6.1 修复 Ability InputReleased 转发

当前 `UFirstAbilitySystemComponent::OnAbilityInputReleased` 只调用了 `AbilitySpecInputReleased`。UE 5.6 的父实现本身会把 `AbilitySpec.InputPressed` 清成 `false`，所以真正缺失的是：**没有向 `UAbilityTask_WaitInputRelease` 触发 GAS 的 `InputReleased` 通用事件**。

下面仍显式写一次 `AbilitySpec->InputPressed = false`，只是为了让自定义输入路由的状态变化一眼可见，并作为安全保险；它不是本问题的根因。

整体替换该函数：

```cpp
void UFirstAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> HandlesToProcess;

	// 与 Pressed 相同，先收集 Handle，避免回调期间可激活列表发生变化。
	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag))
		{
			HandlesToProcess.Add(AbilitySpec.Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : HandlesToProcess)
	{
		FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(Handle);
		if (!AbilitySpec)
		{
			continue;
		}

		AbilitySpec->InputPressed = false;

		if (!AbilitySpec->IsActive())
		{
			continue;
		}

		AbilitySpecInputReleased(*AbilitySpec);

		const TArray<UGameplayAbility*> AbilityInstances =
			AbilitySpec->GetAbilityInstances();

		const FGameplayAbilityActivationInfo& ActivationInfo =
			AbilityInstances.IsEmpty()
				? AbilitySpec->ActivationInfo
				: AbilityInstances.Last()->GetCurrentActivationInfoRef();

		InvokeReplicatedEvent(
			EAbilityGenericReplicatedEvent::InputReleased,
			AbilitySpec->Handle,
			ActivationInfo.GetActivationPredictionKey());
	}
}
```

如果编译器在 `UGameplayAbility` 处提示类型不完整，在 `FirstAbilitySystemComponent.cpp` 顶部增加：

```cpp
#include "Abilities/GameplayAbility.h"
```

### 6.2 修复 Dodge 的双重 EndAbility

当前 `FirstGA_DKDodge.cpp` 的 `EndAbility()` 在开头和结尾各调用了一次父类。一次能力只能结束一次，否则取消链会出现重复清理和难以复现的问题。

整体替换为：

```cpp
void UFirstGA_DKDodge::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 所有清理必须发生在唯一一次 Super::EndAbility 之前。
	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_Invincible);
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

### 编译闸门 1

关闭编辑器，使用 IDE 完整编译 `FirstEditor Win64 Development`。启动后验证：

- 闪避仍能正常开始和结束；
- 长按/松开闪避输入没有新错误；
- 输出日志没有重复结束 Ability 的断言。

---

## 7. 新增 GameplayTag

### 7.1 MyGameplayTags.h

在对应分类中新增：

```cpp
	// 新 Ability 输入。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_GuardParry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Execute);

	// 主角防御与处决 Ability 身份。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_GuardParry);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_GuardBreak);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Ability_Execute);

	// 主角防御事件。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_GuardHit);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_GuardBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ParrySuccess);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Event_ExecutionAbortedByBoss);

	// Defending = 格挡 Ability 整体生命周期；
	// Blocking = 当前真正能抵消伤害；松开时先移除它，再播放 End。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Defending);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Blocking);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_ParryWindow);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_GuardBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_Executing);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DK_Status_StaminaRegenPaused);

	// BOSS 弹反、破韧与处决。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_ParryStagger);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Ability_Executable);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_Parried);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_PoiseBroken);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionStarted);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionAborted);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionFinished);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutionApplyDamage);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Event_ExecutableExpired);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Executable);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_BeingExecuted);
	// 只在处决伤害真正致死时保留，供 GA_Boss_Death 选择配对死亡分支。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Executed);

	// SetByCaller：即时精力、韧性与处决伤害。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_StaminaDelta);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_PoiseDelta);
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_SetByCaller_ExecutionDamage);
```

### 7.2 MyGameplayTags.cpp

```cpp
	UE_DEFINE_GAMEPLAY_TAG(InputTag_GuardParry, "InputTag.GuardParry");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Execute, "InputTag.Execute");

	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_GuardParry, "DK.Ability.GuardParry");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_GuardBreak, "DK.Ability.GuardBreak");
	UE_DEFINE_GAMEPLAY_TAG(DK_Ability_Execute, "DK.Ability.Execute");

	UE_DEFINE_GAMEPLAY_TAG(DK_Event_GuardHit, "DK.Event.GuardHit");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_GuardBroken, "DK.Event.GuardBroken");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ParrySuccess, "DK.Event.ParrySuccess");
	UE_DEFINE_GAMEPLAY_TAG(DK_Event_ExecutionAbortedByBoss, "DK.Event.ExecutionAbortedByBoss");

	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Defending, "DK.Status.Defending");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Blocking, "DK.Status.Blocking");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_ParryWindow, "DK.Status.ParryWindow");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_GuardBroken, "DK.Status.GuardBroken");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_Executing, "DK.Status.Executing");
	UE_DEFINE_GAMEPLAY_TAG(DK_Status_StaminaRegenPaused, "DK.Status.StaminaRegenPaused");

	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_ParryStagger, "Boss.Ability.ParryStagger");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Ability_Executable, "Boss.Ability.Executable");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_Parried, "Boss.Event.Parried");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_PoiseBroken, "Boss.Event.PoiseBroken");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionStarted, "Boss.Event.ExecutionStarted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionAborted, "Boss.Event.ExecutionAborted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionFinished, "Boss.Event.ExecutionFinished");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutionApplyDamage, "Boss.Event.Execution.ApplyDamage");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Event_ExecutableExpired, "Boss.Event.ExecutableExpired");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Executable, "Boss.Status.Executable");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_BeingExecuted, "Boss.Status.BeingExecuted");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Executed, "Boss.Status.Executed");

	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_StaminaDelta, "Combat.SetByCaller.StaminaDelta");
	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_PoiseDelta, "Combat.SetByCaller.PoiseDelta");
	UE_DEFINE_GAMEPLAY_TAG(Combat_SetByCaller_ExecutionDamage, "Combat.SetByCaller.ExecutionDamage");
```

> 旧版文档使用过 `Boss_Event_ExecutionApplyKill / Boss.Event.Execution.ApplyKill`。如果你已经把旧原生 Tag 写进源码，也要在 `MyGameplayTags.h/.cpp` 中同步改为上面的 `ApplyDamage`；以后创建 Montage Notify 时只选择新 Tag，不要保留两个近似事件。

### 编译闸门 2

只新增标签后完整编译一次。原生 GameplayTag 名称写错时，越早发现越容易定位。

---

## 8. 新增攻击防御描述

这一步不是创建继承类，而是创建一个普通的 Unreal 类型头文件：

- 父类：无；文件里定义的是 `UENUM` 和 `USTRUCT`；
- 创建方式：不要使用 UE 的“新建 C++ 类”向导，在 IDE 中新建 Header File；
- `.h` 路径：`Source/First/Public/Types/FirstCombatTypes.h`；
- `.cpp` 路径：不需要。

新建：

`Source/First/Public/Types/FirstCombatTypes.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "FirstCombatTypes.generated.h"

// 一次 BOSS 近战命中经过主角防御组件后的唯一结果。
UENUM(BlueprintType)
enum class EFirstDefenseResult : uint8
{
	// 防御系统没有拦截；攻击 Ability 应继续应用生命伤害。
	Damaged,

	// 本击被普通格挡；不应用生命伤害。
	Blocked,

	// 本击被弹反；不应用生命伤害，并取消攻击/削减 BOSS 韧性。
	Parried,

	// 目标死亡或处于无敌帧；本击无需继续结算。
	Avoided
};

// 每种攻击自己描述“能被怎样防御”，组件不依赖具体 Ability 类名。
USTRUCT(BlueprintType)
struct FFirstMeleeDefenseData
{
	GENERATED_BODY()

	// 白名单默认：漏配置的新招式不可格挡。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense")
	bool bBlockable = false;

	// 白名单默认：漏配置的新招式不可弹反。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense")
	bool bParryable = false;

	// 普通格挡成功时扣除的主角精力。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense", meta=(ClampMin="0.0"))
	float GuardStaminaDamage = 0.f;

	// 弹反成功时扣除的 BOSS 韧性。当前普通攻击和三连都填 50。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense", meta=(ClampMin="0.0"))
	float ParryPoiseDamage = 0.f;
};
```

这一结构默认不可防御，普通攻击和三连要在 `ABossCharacter` 构造函数中显式打开。未来投技直接使用默认结构即可。

### 编译闸门 3

新增这个头文件后完整编译一次，确认 UHT 能生成 `FirstCombatTypes.generated.h`。

---

## 9. 精力、恢复暂停、韧性与处决 GameplayEffect

这一阶段只做“数值工具”，先不接 Ability。每个 GE 都很小，出错时容易定位。

> **UE 5.6 原生 GE 构造函数警告：**不要在 `UGameplayEffect` 派生类的构造函数中调用 `FindOrAddComponent()` 或 `AddComponent()`。找不到现有组件时，它们会通过 `NewObject(..., NAME_None)` 创建对象；构造 CDO 期间这样做会因默认子对象名称为空而让编辑器启动崩溃。构造函数中必须使用带固定名称的 `CreateDefaultSubobject<>()`，并把返回的组件加入受保护的 `GEComponents` 数组。下面三处代码已经全部按这个规则编写。

### 9.1 即时精力变化：FirstGE_StaminaChange

**创建类信息**

- 父类：`UGameplayEffect`（“所有类”中搜索 `GameplayEffect`）；
- 向导类名：`FirstGE_StaminaChange`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/GameplayEffects`；
- 生成结果：`Source/First/Public/AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h` 和 `Source/First/Private/AbilitySystem/GameplayEffects/FirstGE_StaminaChange.cpp`。

新建 `FirstGE_StaminaChange.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaChange.generated.h"

UCLASS()
class FIRST_API UFirstGE_StaminaChange : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_StaminaChange();
};
```

新建 `FirstGE_StaminaChange.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaChange::UFirstGE_StaminaChange()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =
		MyGameplayTags::Combat_SetByCaller_StaminaDelta;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =
		FGameplayEffectModifierMagnitude(SetByCallerMagnitude);

	Modifiers.Add(Modifier);
}
```

调用方会写负数，例如 `-25`。`UFirstAttributeSet` 已把 Stamina 夹紧到 `0 ~ MaxStamina`，所以这里不需要重复 Clamp。

### 9.2 格挡命中后的恢复暂停：FirstGE_StaminaRegenPause

**创建类信息**

- 父类：`UGameplayEffect`（“所有类”中搜索 `GameplayEffect`）；
- 向导类名：`FirstGE_StaminaRegenPause`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/GameplayEffects`；
- 生成结果：`FirstGE_StaminaRegenPause.h` 与对应的 `Source/First/Private/AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.cpp`。

新建 `FirstGE_StaminaRegenPause.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaRegenPause.generated.h"

UCLASS()
class FIRST_API UFirstGE_StaminaRegenPause : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_StaminaRegenPause();
};
```

新建 `FirstGE_StaminaRegenPause.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaRegenPause::UFirstGE_StaminaRegenPause()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude =
		FGameplayEffectModifierMagnitude(FScalableFloat(0.75f));

	// 同一目标只保留一层；新一次格挡会刷新剩余 0.75 秒。
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	StackLimitCount = 1;
	StackDurationRefreshPolicy =
		EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackExpirationPolicy =
		EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	// UE 5.6 使用 GameplayEffectComponent 授予标签，
	// 不再写已弃用的 InheritableOwnedTagsContainer。
	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(MyGameplayTags::DK_Status_StaminaRegenPaused);

	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			TEXT("TargetTagsComponent"));
	GEComponents.Add(TargetTagsComponent);
	TargetTagsComponent->SetAndApplyTargetTagChanges(GrantedTags);
}
```

用持续 GE 而不是组件手写 Timer 有两个优点：

- 三连连续格挡时，GAS 自动刷新同一层的 0.75 秒；
- GE 到期后自动移除标签，不会因为角色死亡或组件销毁留下计时器。

### 9.3 格挡期间慢恢复：FirstGE_StaminaRegenGuarding

**创建类信息**

- 父类：`UGameplayEffect`（“所有类”中搜索 `GameplayEffect`）；
- 向导类名：`FirstGE_StaminaRegenGuarding`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/GameplayEffects`；
- 生成结果：`FirstGE_StaminaRegenGuarding.h` 与对应的 `Source/First/Private/AbilitySystem/GameplayEffects/FirstGE_StaminaRegenGuarding.cpp`。

新建 `FirstGE_StaminaRegenGuarding.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaRegenGuarding.generated.h"

UCLASS()
class FIRST_API UFirstGE_StaminaRegenGuarding : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_StaminaRegenGuarding();
};
```

新建 `FirstGE_StaminaRegenGuarding.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenGuarding.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"

UFirstGE_StaminaRegenGuarding::UFirstGE_StaminaRegenGuarding()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(0.5f);

	// 每 0.5 秒 +1，即每秒 +2。
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =
		FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	Modifiers.Add(Modifier);

	UTargetTagRequirementsGameplayEffectComponent* Requirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(
			TEXT("GuardingTagRequirementsComponent"));
	GEComponents.Add(Requirements);

	Requirements->OngoingTagRequirements.RequireTags.AddTag(
		MyGameplayTags::DK_Status_Blocking);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::DK_Status_StaminaRegenPaused);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::DK_Status_GuardBroken);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::Shared_Status_Dead);
}
```

### 9.4 修改现有普通精力恢复

保留现有 `Period = 0.5`、每次 `+10`。在 `FirstGE_StaminaRegen.cpp` 顶部新增：

```cpp
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "MyGameplayTags.h"
```

在构造函数末尾、`Modifiers.Add(RegenModifier);` 后增加：

```cpp
	UTargetTagRequirementsGameplayEffectComponent* Requirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(
			TEXT("NormalRegenTagRequirementsComponent"));
	GEComponents.Add(Requirements);

	// Blocking 存在时关掉 20/秒的普通恢复，避免与 2/秒慢恢复叠加。
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::DK_Status_Blocking);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::DK_Status_StaminaRegenPaused);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::DK_Status_GuardBroken);
	Requirements->OngoingTagRequirements.IgnoreTags.AddTag(
		MyGameplayTags::Shared_Status_Dead);
```

> UE 5.6 注意：不要继续写 `UGameplayEffect::OngoingTagRequirements`，它从 5.3 起已弃用。本册使用 `UTargetTagRequirementsGameplayEffectComponent`。这里同样必须通过带名称的 `CreateDefaultSubobject` 创建，不能换回 `FindOrAddComponent`。

两个无限恢复 GE 会同时常驻 ASC，但任意时刻只有一个处于有效状态：

| 当前 Tag | 普通恢复 | 格挡慢恢复 |
|---|---:|---:|
| 无 Blocking | 20/秒 | 关闭 |
| Blocking | 关闭 | 2/秒 |
| StaminaRegenPaused | 关闭 | 关闭 |
| GuardBroken | 关闭 | 关闭 |
| Dead | 关闭 | 关闭 |

### 9.5 即时韧性变化：FirstGE_PoiseChange

**创建类信息**

- 父类：`UGameplayEffect`（“所有类”中搜索 `GameplayEffect`）；
- 向导类名：`FirstGE_PoiseChange`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/GameplayEffects`；
- 生成结果：`FirstGE_PoiseChange.h` 与对应的 `Source/First/Private/AbilitySystem/GameplayEffects/FirstGE_PoiseChange.cpp`。

新建 `FirstGE_PoiseChange.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_PoiseChange.generated.h"

UCLASS()
class FIRST_API UFirstGE_PoiseChange : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_PoiseChange();
};
```

新建 `FirstGE_PoiseChange.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_PoiseChange.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "MyGameplayTags.h"

UFirstGE_PoiseChange::UFirstGE_PoiseChange()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =
		MyGameplayTags::Combat_SetByCaller_PoiseDelta;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetPoiseAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =
		FGameplayEffectModifierMagnitude(SetByCallerMagnitude);
	Modifiers.Add(Modifier);
}
```

弹反写 `-50`，超时回满写 `MaxPoise - CurrentPoise`。这样两条路径都经过 AttributeSet 的夹紧和现有韧性 UI 广播。

### 9.6 处决原始伤害：FirstGE_ExecutionDamage

不要直接 `SetHealth(0)`，也不要把 `AttackPower × 10` 填进普通攻击的 `BaseDamage`。直接写 Health 会绕过当前 `DamageTaken → Shared.Status.Dead → GA_Boss_Death` 链；而普通 `UGEExecCalc_DamageTaken` 还会再次乘 `AttackPower` 并除以 `DefensePower`，最终会变成错误的二次乘算。

**创建类信息**

- 父类：`UGameplayEffect`（“所有类”中搜索 `GameplayEffect`）；
- 向导类名：`FirstGE_ExecutionDamage`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/GameplayEffects`；
- 生成结果：`FirstGE_ExecutionDamage.h` 与对应的 `Source/First/Private/AbilitySystem/GameplayEffects/FirstGE_ExecutionDamage.cpp`。

新建 `FirstGE_ExecutionDamage.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_ExecutionDamage.generated.h"

UCLASS()
class FIRST_API UFirstGE_ExecutionDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFirstGE_ExecutionDamage();
};
```

新建 `FirstGE_ExecutionDamage.cpp`：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_ExecutionDamage.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "MyGameplayTags.h"

UFirstGE_ExecutionDamage::UFirstGE_ExecutionDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat SetByCallerMagnitude;
	SetByCallerMagnitude.DataTag =
		MyGameplayTags::Combat_SetByCaller_ExecutionDamage;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFirstAttributeSet::GetDamageTakenAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude =
		FGameplayEffectModifierMagnitude(SetByCallerMagnitude);
	Modifiers.Add(Modifier);
}
```

处决能力会在动画命中帧计算：

```text
ExecutionDamage = max(玩家当前 AttackPower, 0) × ExecutionDamageMultiplier
ExecutionDamageMultiplier 首版默认 10.0
```

这个数值是**原始处决伤害**：不读取武器 `WeaponBaseDamage`，不乘 `ComboMultiplier`，也不除以 BOSS 的 `DefensePower`。它还会绕过普通伤害 ExecCalc 内的无敌判断；这是有意设计，因为事件只允许在 `Boss.Status.Executable + Boss.Status.BeingExecuted` 的配对流程中触发，当前 BOSS 也没有处决阶段无敌规则。以后若给 BOSS 增加特殊无敌阶段，应在 `HandleExecutionApplyDamage` 的资格检查中明确决定是否允许处决，而不是悄悄改普通伤害公式。

GE 只负责把已经算好的正数写入 `DamageTaken`，因此血条、受伤结算、`Shared.Status.Dead` 和死亡 Ability 仍复用现有链路，但不再保证每次处决都能击杀。

> 如果你已经按旧版创建了 `FirstGE_ExecutionKill.h/.cpp`，现在正是重命名成本最低的阶段：关闭 UE，把 `.h/.cpp` 文件名、类名和构造函数统一改成 `FirstGE_ExecutionDamage`，并把头文件中的 `#include "FirstGE_ExecutionKill.generated.h"` 改成 `#include "FirstGE_ExecutionDamage.generated.h"`，再完整编译。`generated.h` 是 UHT 生成内容，不是需要手动改名的实体源文件。不要同时保留 Kill 与 Damage 两套类。

### 编译闸门 4

新增以上 GE 并修改普通恢复后，关闭编辑器完整编译。此时还不要把新 GE 填入数据资产。编译通过后再进入防御组件。

---

## 10. 新增 UDKDefenseComponent

这个组件没有 Tick。每次只有在 BOSS 武器真实命中主角时才调用一次。

### 10.1 DKDefenseComponent.h

**创建类信息**

- 父类：`UActorComponent`（向导常用父类中选择 `Actor Component`，或在“所有类”中搜索 `ActorComponent`）；
- 向导类名：`DKDefenseComponent`；
- 类类型：`Public`；
- Path：`Source/First/Public/Components/Combat`；
- 生成结果：`Source/First/Public/Components/Combat/DKDefenseComponent.h` 和 `Source/First/Private/Components/Combat/DKDefenseComponent.cpp`。

新建：

`Source/First/Public/Components/Combat/DKDefenseComponent.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/FirstCombatTypes.h"
#include "DKDefenseComponent.generated.h"

class UAbilitySystemComponent;

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class FIRST_API UDKDefenseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDKDefenseComponent();

	// BOSS 攻击 GA 在真正应用生命伤害前调用一次。
	EFirstDefenseResult ResolveIncomingMeleeAttack(
		AActor* Attacker,
		const FFirstMeleeDefenseData& AttackData);

	FORCEINLINE float GetMinimumStaminaToGuard() const
	{
		return MinimumStaminaToGuard;
	}

	FORCEINLINE float GetParryWindowDuration() const
	{
		return ParryWindowDuration;
	}

	FORCEINLINE float GetGuardMoveSpeed() const
	{
		return GuardMoveSpeed;
	}

	FORCEINLINE float GetGuardBreakDuration() const
	{
		return GuardBreakDuration;
	}

private:
	bool IsAttackerInsideGuardArc(const AActor* Attacker) const;
	void ApplyStaminaDelta(UAbilitySystemComponent* ASC, AActor* Attacker, float Delta) const;
	void ApplyStaminaRegenPause(UAbilitySystemComponent* ASC, AActor* Attacker) const;

	// 这里保存半角。60° 表示总格挡范围 120°。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0"))
	float GuardHalfAngleDegrees = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MinimumStaminaToGuard = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Parry",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ParryWindowDuration = 0.15f;

	// 当前没有格挡移动动画，首版设 0 防止滑步。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float GuardMoveSpeed = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard Break",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float GuardBreakDuration = 1.3f;
};
```

### 10.2 DKDefenseComponent.cpp

新建：

`Source/First/Private/Components/Combat/DKDefenseComponent.cpp`

```cpp
#include "Components/Combat/DKDefenseComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaRegenPause.h"
#include "MyGameplayTags.h"

UDKDefenseComponent::UDKDefenseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UDKDefenseComponent::IsAttackerInsideGuardArc(const AActor* Attacker) const
{
	const AActor* Defender = GetOwner();
	if (!Defender || !Attacker)
	{
		return false;
	}

	FVector Forward = Defender->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();

	FVector ToAttacker =
		Attacker->GetActorLocation() - Defender->GetActorLocation();
	ToAttacker.Z = 0.f;

	// 两个 Actor 几乎重合时无法得到稳定方向；按正面处理更安全。
	if (ToAttacker.IsNearlyZero())
	{
		return true;
	}

	ToAttacker.Normalize();

	const float MinimumDot = FMath::Cos(
		FMath::DegreesToRadians(GuardHalfAngleDegrees));

	return FVector::DotProduct(Forward, ToAttacker) >= MinimumDot;
}

void UDKDefenseComponent::ApplyStaminaDelta(
	UAbilitySystemComponent* ASC,
	AActor* Attacker,
	float Delta) const
{
	if (!ASC || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Attacker);
	Context.AddInstigator(Attacker, Attacker);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UFirstGE_StaminaChange::StaticClass(),
		1.f,
		Context);

	if (!Spec.IsValid())
	{
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(
		MyGameplayTags::Combat_SetByCaller_StaminaDelta,
		Delta);

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UDKDefenseComponent::ApplyStaminaRegenPause(
	UAbilitySystemComponent* ASC,
	AActor* Attacker) const
{
	if (!ASC)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Attacker);
	Context.AddInstigator(Attacker, Attacker);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UFirstGE_StaminaRegenPause::StaticClass(),
		1.f,
		Context);

	if (Spec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

EFirstDefenseResult UDKDefenseComponent::ResolveIncomingMeleeAttack(
	AActor* Attacker,
	const FFirstMeleeDefenseData& AttackData)
{
	AActor* Defender = GetOwner();
	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Defender);

	if (!Defender || !Attacker || !ASC)
	{
		return EFirstDefenseResult::Damaged;
	}

	// 死亡和闪避无敌优先于防御；不播放格挡/弹反表现。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
		ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Invincible))
	{
		return EFirstDefenseResult::Avoided;
	}

	if (!IsAttackerInsideGuardArc(Attacker))
	{
		return EFirstDefenseResult::Damaged;
	}

	// 弹反优先于 Blocking。按下前 0.15 秒两个标签会同时存在。
	if (AttackData.bParryable &&
		ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_ParryWindow))
	{
		FGameplayEventData PlayerEvent;
		PlayerEvent.Instigator = Attacker;
		PlayerEvent.Target = Defender;
		PlayerEvent.EventMagnitude = AttackData.ParryPoiseDamage;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Defender,
			MyGameplayTags::DK_Event_ParrySuccess,
			PlayerEvent);

		FGameplayEventData BossEvent;
		BossEvent.Instigator = Defender;
		BossEvent.Target = Attacker;
		BossEvent.EventMagnitude = AttackData.ParryPoiseDamage;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Attacker,
			MyGameplayTags::Boss_Event_Parried,
			BossEvent);

		return EFirstDefenseResult::Parried;
	}

	const float OldStamina = ASC->GetNumericAttribute(
		UFirstAttributeSet::GetStaminaAttribute());

	if (AttackData.bBlockable &&
		OldStamina > 0.f &&
		ASC->HasMatchingGameplayTag(MyGameplayTags::DK_Status_Blocking))
	{
		const float StaminaDamage =
			FMath::Max(AttackData.GuardStaminaDamage, 0.f);

		ApplyStaminaDelta(ASC, Attacker, -StaminaDamage);
		ApplyStaminaRegenPause(ASC, Attacker);

		const float NewStamina = ASC->GetNumericAttribute(
			UFirstAttributeSet::GetStaminaAttribute());

		FGameplayEventData BlockEvent;
		BlockEvent.Instigator = Attacker;
		BlockEvent.Target = Defender;
		BlockEvent.EventMagnitude = StaminaDamage;

		if (NewStamina <= 0.f)
		{
			// 这一击仍返回 Blocked；破防 Ability 会立刻移除 Blocking，
			// 所以后续三连段数才能正常扣血。
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Defender,
				MyGameplayTags::DK_Event_GuardBroken,
				BlockEvent);
		}
		else
		{
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Defender,
				MyGameplayTags::DK_Event_GuardHit,
				BlockEvent);
		}

		return EFirstDefenseResult::Blocked;
	}

	return EFirstDefenseResult::Damaged;
}
```

### 10.3 正面判断为什么不用锁定目标

防御方向使用：

```text
主角 ActorForwardVector
对比
主角位置 → 本次真实 Attacker 位置
```

不要读取 `UDKTargetLockComponent::CurrentTarget`。否则会出现：玩家锁定 A，BOSS B 从正面砍来，却因为锁定目标不是 B 而无法格挡。

### 10.4 为什么无敌直接返回 Avoided

现有 `UGEExecCalc_DamageTaken` 已经会忽略 `DK.Status.Invincible`。组件提前返回 `Avoided` 是为了不再创建无意义的伤害 Spec，也不会在闪避无敌期间错误播放格挡或弹反表现。普通非 BOSS 伤害仍由原来的 ExecCalc 兜底。

---

## 11. 把防御组件和 Montage 接到 ADKCharacter

### 11.1 修改 DKCharacter.h

在前置声明区域增加：

```cpp
class UDKDefenseComponent;
```

在 `DKCombatComponent` 附近增加：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Defense",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKDefenseComponent> DKDefenseComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> GuardParryMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Defense|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> GuardBreakMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Execution|Anim",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionMontage;

	// 处决伤害 = 命中帧时玩家当前 AttackPower × 此倍率。
	// 放在玩家角色上，避免由受击的 BOSS 决定玩家处决伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Execution",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float ExecutionDamageMultiplier = 10.f;
```

在 public Getter 区域增加：

```cpp
	FORCEINLINE UDKDefenseComponent* GetDKDefenseComponent() const
	{
		return DKDefenseComponent;
	}

	FORCEINLINE UAnimMontage* GetGuardParryMontage() const
	{
		return GuardParryMontage;
	}

	FORCEINLINE UAnimMontage* GetGuardBreakMontage() const
	{
		return GuardBreakMontage;
	}

	FORCEINLINE UAnimMontage* GetExecutionMontage() const
	{
		return ExecutionMontage;
	}

	FORCEINLINE float GetExecutionDamageMultiplier() const
	{
		return ExecutionDamageMultiplier;
	}
```

倍率放在 `ADKCharacter` 而不是 `ABossCharacter`：它属于玩家处决能力的伤害规则，并且能直接在 `BP_DKCharacter → 类默认值`中调整。读取的是 Notify 到达那一帧的**运行时 AttackPower**，因此以后临时攻击力 Buff 也会自然影响处决伤害。

### 11.2 修改 DKCharacter.cpp

顶部增加：

```cpp
#include "Components/Combat/DKDefenseComponent.h"
```

构造函数中，创建 `DKCombatComponent` 的位置附近增加：

```cpp
	DKDefenseComponent =
		CreateDefaultSubobject<UDKDefenseComponent>(TEXT("DKDefenseComponent"));
```

不要在 `BeginPlay` 中 `NewObject`。组件是角色的默认子对象，编辑器才能在 `BP_DKCharacter` 类默认值中显示和保存配置。

### 编译闸门 5

关闭编辑器完整编译。重新打开 `BP_DKCharacter`，在组件面板中应该能看到 `DKDefenseComponent`，详情中能看到：

```text
Guard Half Angle Degrees = 60
Minimum Stamina To Guard = 20
Parry Window Duration = 0.15
Guard Move Speed = 0
Guard Break Duration = 1.3
```

此时还没有 Ability 调用它，游戏行为应与之前完全相同。

---

## 12. 主角按住格挡与弹反：UFirstGA_DKGuardParry

首版使用一个 Montage、多段 Section。不要在同一个长期 Guard Ability 中连续创建多个 `PlayMontageAndWait`；新 Montage 会把旧任务判成 Interrupted，容易错误结束整个格挡。

### 12.1 FirstGA_DKGuardParry.h

**创建类信息**

- 父类：`UFirstDKGameplayAbility`（进入“所有类”，搜索 `FirstDKGameplayAbility`）；
- 向导类名：`FirstGA_DKGuardParry`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/Abilities/DK`；
- 生成结果：`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h` 和对应的 `Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.cpp`。

新建：

`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKGuardParry.generated.h"

class UAnimMontage;

UCLASS()
class FIRST_API UFirstGA_DKGuardParry : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstGA_DKGuardParry();

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleParryWindowElapsed();

	UFUNCTION()
	void HandleInputReleased(float TimeHeld);

	UFUNCTION()
	void HandleGuardHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleParrySuccess(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void CancelActiveAttackAbilities();
	void RemoveBlockingTag();
	void RemoveParryWindowTag();
	void JumpToGuardSection(FName SectionName);
	void BeginGuardExit();
	void FinishGuard(bool bWasCancelled);

	bool bOwnsBlockingTag = false;
	bool bOwnsParryWindowTag = false;
	bool bExitRequested = false;
	bool bSavedMovementState = false;
	bool bTargetLockWasActiveAtStart = false;

	bool bSavedOrientRotationToMovement = true;
	bool bSavedUseControllerDesiredRotation = false;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGuardMontage;
};
```

### 12.2 FirstGA_DKGuardParry.cpp

新建：

`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.cpp`

```cpp
#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardParry.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKCombatComponent.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UFirstGA_DKGuardParry::UFirstGA_DKGuardParry()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bIsCancelable = true;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_GuardParry);
	SetAssetTags(AssetTags);

	// Defending 覆盖 Start、Loop、Hit、Parry 和 End 整个生命周期。
	// 真正抵消伤害的 Blocking 由本类用 Loose Tag 单独控制。
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	// 首版允许闪避取消整个 GuardParry 生命周期。
	// Dodge 仍会先通过 Cost/Cooldown 并成功 Commit，之后才真正取消 Guard。
	ActivationOwnedTags.AddTag(
		MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);

	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	// 不要把 DK.Status.Attacking 加到 ActivationBlockedTags。
	// 攻击中能否转格挡，要在 CanActivateAbility 中读取 .By.Parry 窗口。
}

bool UFirstGA_DKGuardParry::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const ADKCharacter* DK = ActorInfo
		? Cast<ADKCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;

	if (!DK || !ASC || !DK->GetDKDefenseComponent())
	{
		return false;
	}

	const UCharacterMovementComponent* Movement =
		DK->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}

	// 首版是“持剑格挡”，未装备剑时不能启动。
	const UDKCombatComponent* Combat = DK->GetDKCombatComponent();
	if (!Combat ||
		Combat->CurrentEquippedWeaponTag != MyGameplayTags::DK_Weapon_Sword)
	{
		return false;
	}

	const float CurrentStamina = ASC->GetNumericAttribute(
		UFirstAttributeSet::GetStaminaAttribute());
	if (CurrentStamina <
		DK->GetDKDefenseComponent()->GetMinimumStaminaToGuard())
	{
		return false;
	}

	const bool bIsAttacking = ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Attacking);

	if (!bIsAttacking)
	{
		return true;
	}

	// 攻击中只有动画明确开放 By.Parry 取消窗口时才允许举剑。
	return ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Action_Cancelable_By_Parry);
}

void UFirstGA_DKGuardParry::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	UDKDefenseComponent* Defense =
		DK ? DK->GetDKDefenseComponent() : nullptr;

	if (!DK || !ASC || !Defense)
	{
		FinishGuard(true);
		return;
	}

	// 虽然当前没有 Cost/Cooldown，仍保留标准提交点，方便以后扩展。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishGuard(true);
		return;
	}

	// 清掉同一帧可能已由原生 Jump() 设置、但移动组件尚未消费的 pending jump。
	DK->StopJumping();

	// 只有 Guard 已成功提交后才取消当前攻击，避免激活失败却中断攻击。
	CancelActiveAttackAbilities();

	bExitRequested = false;
	bTargetLockWasActiveAtStart =
		DK->GetTargetLockComponent() &&
		DK->GetTargetLockComponent()->IsTargetLocked();

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
	bOwnsBlockingTag = true;

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	bOwnsParryWindowTag = true;

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation =
			Movement->bUseControllerDesiredRotation;
		bSavedMovementState = true;

		Movement->StopMovementImmediately();
		Movement->MaxWalkSpeed = Defense->GetGuardMoveSpeed();
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
	}

	// 监听输入松开。true 表示如果松开边沿比任务建立更早，也立即回调。
	UAbilityTask_WaitInputRelease* ReleaseTask =
		UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	ReleaseTask->OnRelease.AddDynamic(
		this,
		&ThisClass::HandleInputReleased);
	ReleaseTask->ReadyForActivation();

	// 弹反窗口只存在前 0.15 秒；Blocking 不受这个 Delay 影响。
	UAbilityTask_WaitDelay* ParryWindowTask =
		UAbilityTask_WaitDelay::WaitDelay(
			this,
			Defense->GetParryWindowDuration());
	ParryWindowTask->OnFinish.AddDynamic(
		this,
		&ThisClass::HandleParryWindowElapsed);
	ParryWindowTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* GuardHitTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_GuardHit,
			nullptr,
			false,
			true);
	GuardHitTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleGuardHit);
	GuardHitTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ParrySuccessTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ParrySuccess,
			nullptr,
			false,
			true);
	ParrySuccessTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleParrySuccess);
	ParrySuccessTask->ReadyForActivation();

	ActiveGuardMontage = DK->GetGuardParryMontage();
	if (!ActiveGuardMontage)
	{
		// 没有表现资源时仍允许测试标签和数值；松开后正常结束。
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("GuardParryMontage"),
			ActiveGuardMontage,
			1.f,
			TEXT("Start"));

	MontageTask->OnCompleted.AddDynamic(
		this,
		&ThisClass::HandleMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(
		this,
		&ThisClass::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(
		this,
		&ThisClass::HandleMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UFirstGA_DKGuardParry::HandleParryWindowElapsed()
{
	RemoveParryWindowTag();
}

void UFirstGA_DKGuardParry::HandleInputReleased(float TimeHeld)
{
	BeginGuardExit();
}

void UFirstGA_DKGuardParry::HandleGuardHit(FGameplayEventData Payload)
{
	if (!bExitRequested)
	{
		JumpToGuardSection(TEXT("Hit"));
	}
}

void UFirstGA_DKGuardParry::HandleParrySuccess(FGameplayEventData Payload)
{
	if (!bExitRequested)
	{
		JumpToGuardSection(TEXT("Parry"));
	}
}

void UFirstGA_DKGuardParry::HandleMontageCompleted()
{
	FinishGuard(false);
}

void UFirstGA_DKGuardParry::HandleMontageInterrupted()
{
	FinishGuard(true);
}

void UFirstGA_DKGuardParry::CancelActiveAttackAbilities()
{
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MyGameplayTags::DK_Ability_Attack);
	ASC->CancelAbilities(&AttackTags, nullptr, this);
}

void UFirstGA_DKGuardParry::RemoveBlockingTag()
{
	if (!bOwnsBlockingTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_Blocking);
	}

	bOwnsBlockingTag = false;
}

void UFirstGA_DKGuardParry::RemoveParryWindowTag()
{
	if (!bOwnsParryWindowTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_ParryWindow);
	}

	bOwnsParryWindowTag = false;
}

void UFirstGA_DKGuardParry::JumpToGuardSection(FName SectionName)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UAnimInstance* AnimInstance =
		DK && DK->GetMesh() ? DK->GetMesh()->GetAnimInstance() : nullptr;

	if (!AnimInstance || !ActiveGuardMontage ||
		ActiveGuardMontage->GetSectionIndex(SectionName) == INDEX_NONE)
	{
		return;
	}

	AnimInstance->Montage_JumpToSection(SectionName, ActiveGuardMontage);
}

void UFirstGA_DKGuardParry::BeginGuardExit()
{
	if (bExitRequested)
	{
		return;
	}

	bExitRequested = true;

	// 松开第一帧就失去防御资格；End Section 只是表现。
	RemoveParryWindowTag();
	RemoveBlockingTag();

	if (!ActiveGuardMontage ||
		ActiveGuardMontage->GetSectionIndex(TEXT("End")) == INDEX_NONE)
	{
		FinishGuard(false);
		return;
	}

	JumpToGuardSection(TEXT("End"));
}

void UFirstGA_DKGuardParry::FinishGuard(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

void UFirstGA_DKGuardParry::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	RemoveParryWindowTag();
	RemoveBlockingTag();

	if (ADKCharacter* DK = GetDKCharacterFromActorInfo())
	{
		if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement();
			bSavedMovementState && Movement)
		{
			// 不恢复进入 Guard 时的旧快照。Shift 可能在 Guard 期间按下或松开，
			// 应根据 ADKCharacter 当前保存的跑步状态重新决定 250/500。
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();

			const bool bTargetLockIsActiveNow =
				DK->GetTargetLockComponent() &&
				DK->GetTargetLockComponent()->IsTargetLocked();

			// 锁定模式若在 Guard 期间因目标死亡而自动变化，
			// TargetLockComponent 已经恢复了正确朝向设置，此处不要用旧快照覆盖。
			if (bTargetLockIsActiveNow == bTargetLockWasActiveAtStart)
			{
				Movement->bOrientRotationToMovement =
					bSavedOrientRotationToMovement;
				Movement->bUseControllerDesiredRotation =
					bSavedUseControllerDesiredRotation;
			}
		}
	}

	bSavedMovementState = false;
	bTargetLockWasActiveAtStart = false;
	ActiveGuardMontage = nullptr;

	// 全函数只能有这一次父类结束调用。
	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

### 12.3 Montage Section 必须这样连

创建一个 `AM_DK_GuardParry`，使用 `DefaultSlot`，包含：

```text
Start   → Loop
Loop    → Loop
Hit     → Loop
Parry   → Loop
End     → 无
```

如果 `Loop` 没有链接回自己，按住右键时 Montage 播完会触发 `OnCompleted`，Guard Ability 会自然结束，看起来就像“只能格挡一下”。

如果 `End` 又链接回 `Loop`，松开后会永远无法结束。

### 12.4 为什么 Blocking 不是 ActivationOwnedTags

`DK.Status.Defending` 适合放在 `ActivationOwnedTags`，因为它与整个 Ability 同生共死。

`DK.Status.Blocking` 不适合，因为：

```text
松开右键
→ 必须立刻不能挡伤害
→ 但 End 收势动画仍需让 Ability 再活一小段时间
```

所以 Blocking 用本类拥有计数的 Loose Tag，并在所有退出路径兜底移除。`bOwnsBlockingTag` 防止误删其它系统以后可能添加的同名 Tag 层数。

---

## 13. 玩家破防：UFirstGA_DKGuardBreak

破防使用独立的 Reactive Ability。防御组件只发送事件，不直接播放动画；GuardBreak Ability 负责取消格挡、锁移动和清理。

### 13.1 FirstGA_DKGuardBreak.h

**创建类信息**

- 父类：`UFirstDKGameplayAbility`（进入“所有类”，搜索 `FirstDKGameplayAbility`）；
- 向导类名：`FirstGA_DKGuardBreak`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/Abilities/DK`；
- 生成结果：`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.h` 和对应的 `Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.cpp`。

新建：

`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKGuardBreak.generated.h"

UCLASS()
class FIRST_API UFirstGA_DKGuardBreak : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstGA_DKGuardBreak();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleBreakFinished();

	void FinishGuardBreak(bool bWasCancelled);

	bool bSavedMovementMode = false;
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	uint8 SavedCustomMovementMode = 0;
};
```

### 13.2 FirstGA_DKGuardBreak.cpp

新建：

`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.cpp`

```cpp
#include "AbilitySystem/Abilities/DK/FirstGA_DKGuardBreak.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "MyGameplayTags.h"

UFirstGA_DKGuardBreak::UFirstGA_DKGuardBreak()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_GuardBreak);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::DK_Event_GuardBroken;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UFirstGA_DKGuardBreak::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	UDKDefenseComponent* Defense =
		DK ? DK->GetDKDefenseComponent() : nullptr;

	if (!DK || !ASC || !Defense)
	{
		FinishGuardBreak(true);
		return;
	}

	// 破防事件可能与 Space 输入落在同一帧；禁止恢复移动后补跳。
	DK->StopJumping();

	// 取消长期 Guard 会通过它自己的 EndAbility 立即移除 Blocking/ParryWindow。
	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_GuardParry);
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_Attack);
	AbilitiesToCancel.AddTag(MyGameplayTags::DK_Ability_Dodge);
	ASC->CancelAbilities(&AbilitiesToCancel, nullptr, this);

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedCustomMovementMode = Movement->CustomMovementMode;
		bSavedMovementMode = true;

		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	const float BreakDuration = FMath::Max(
		Defense->GetGuardBreakDuration(),
		0.01f);

	if (UAnimMontage* Montage = DK->GetGuardBreakMontage())
	{
		// Delay 是玩法权威时长；按动画长度计算 Rate，让表现尽量同步。
		const float PlayRate = Montage->GetPlayLength() > 0.f
			? Montage->GetPlayLength() / BreakDuration
			: 1.f;

		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				TEXT("GuardBreakMontage"),
				Montage,
				PlayRate);
		MontageTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* DelayTask =
		UAbilityTask_WaitDelay::WaitDelay(this, BreakDuration);
	DelayTask->OnFinish.AddDynamic(
		this,
		&ThisClass::HandleBreakFinished);
	DelayTask->ReadyForActivation();
}

void UFirstGA_DKGuardBreak::HandleBreakFinished()
{
	FinishGuardBreak(false);
}

void UFirstGA_DKGuardBreak::FinishGuardBreak(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

void UFirstGA_DKGuardBreak::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();

	const bool bIsDead = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	if (DK && bSavedMovementMode && !bIsDead)
	{
		if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();
			Movement->SetMovementMode(
				SavedMovementMode,
				SavedCustomMovementMode);
		}
	}

	bSavedMovementMode = false;

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

### 13.3 首版动作互斥

首版采用下面的方向性互斥：

```text
格挡 → 可以被闪避取消
格挡 → 不能被普通攻击、装备或收剑取消
破防僵直 → 不能被闪避取消
处决过程 → 不能被闪避取消
闪避启动失败 → 保持原格挡，不得先取消再失败
```

这不是“双向都放开”。Guard 仍然阻止攻击和装备操作，只把 Dodge 设为一个明确的高优先级退出方式。

#### 13.3.1 攻击与装备仍然禁止在格挡中启动

在以下 Ability 构造函数的 `ActivationBlockedTags` 中加入：

```cpp
ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
```

需要检查的能力：

- `UFirstGA_DKLightAttack`；
- `UFirstGA_DKEquipSword`；
- `UFirstGA_DKUnequipSword`。

不要把 `UFirstGA_DKDodge` 放进这个列表。Dodge 仍需阻止破防和处决，但不能再被 `DK.Status.Defending` 直接挡住。

#### 13.3.2 Guard 明确授予 `.By.Dodge` 取消权限

第 12.2 节的 `UFirstGA_DKGuardParry` 构造函数应已经包含：

```cpp
	ActivationOwnedTags.AddTag(
		MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);
```

它表达的是“当前 Guard 可以被 Dodge 取消”，而不是让 Dodge 根据某个具体 Montage 名称猜测状态。首版让这个权限覆盖 Guard 的 `Start / Loop / Hit / Parry / End` 整个 Ability 生命周期，以保证输入响应一致。

如果以后希望成功弹反后的短反馈动画不可取消，再单独增加一个“弹反反馈锁定”状态；不要在首版用 Montage Section 名称硬编码判断。

#### 13.3.3 从 Dodge 的 BlockedTags 移除 Defending

文件：

`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKDodge.cpp`

在 `UFirstGA_DKDodge` 构造函数中删除：

```cpp
	ActivationBlockedTags.AddTag(
		MyGameplayTags::DK_Status_Defending);
```

仍然保留：

```cpp
	ActivationBlockedTags.AddTag(
		MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(
		MyGameplayTags::DK_Status_Executing);
```

否则破防僵直和处决也会被闪避输入直接逃掉。

#### 13.3.4 让 Dodge 同时识别攻击与格挡取消权限

整体替换 `UFirstGA_DKDodge::CanActivateAbility()`：

```cpp
bool UFirstGA_DKDodge::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	// 这里也会检查死亡、重复闪避、换武器、Cost 和 Cooldown。
	if (!Super::CanActivateAbility(
			Handle,
			ActorInfo,
			SourceTags,
			TargetTags,
			OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;

	if (!ASC)
	{
		return false;
	}

	const bool bIsAttacking = ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Attacking);
	const bool bIsDefending = ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Defending);

	// 普通移动状态下不需要取消权限。
	if (!bIsAttacking && !bIsDefending)
	{
		return true;
	}

	// 攻击只在动画的 .By.Dodge 窗口授予该 Tag；
	// Guard 则在整个 Ability 生命周期持有该 Tag。
	return ASC->HasMatchingGameplayTag(
		MyGameplayTags::DK_Status_Action_Cancelable_By_Dodge);
}
```

#### 13.3.5 Dodge 成功提交后再取消 Attack 与 Guard

文件：

`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKDodge.h`

把 private 区域原来的：

```cpp
	void CancelActiveAttackAbilities();
```

改为：

```cpp
	// 只在 Dodge 成功 Commit 后调用；取消当前允许被闪避打断的动作。
	void CancelDodgeInterruptibleAbilities();
```

同时把 `CanActivateAbility()` 上方旧注释“攻击状态必须拥有 `.By.Dodge`”更新为：

```cpp
	// 普通状态可闪避；攻击或格挡状态必须拥有 .By.Dodge 权限。
```

回到：

`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKDodge.cpp`

在 `ActivateAbility()` 中，把：

```cpp
	CancelActiveAttackAbilities();
```

改为：

```cpp
	CancelDodgeInterruptibleAbilities();
```

它上方的注释也应同步改为“成功 Commit 后才取消允许被 Dodge 打断的 Attack / Guard”，避免以后维护时又把取消调用提前。

并整体替换旧函数实现：

```cpp
void UFirstGA_DKDodge::CancelDodgeInterruptibleAbilities()
{
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(
		MyGameplayTags::DK_Ability_Attack);
	AbilitiesToCancel.AddTag(
		MyGameplayTags::DK_Ability_GuardParry);

	// this 防止取消刚刚启动的 Dodge 自己。
	ASC->CancelAbilities(&AbilitiesToCancel, nullptr, this);
}
```

调用位置必须继续放在 `CommitAbility()` 成功之后：

```text
Dodge 先通过 Cost / Cooldown
→ Commit 成功并扣除闪避精力
→ 再取消 GuardParry
→ GuardParry::EndAbility 清除 Blocking / ParryWindow / 慢恢复
→ 播放或执行闪避位移
```

如果精力不足或 Cooldown 未结束，Dodge 不会成功 Commit，也就不会取消当前格挡。这可以避免“按了闪避、既没闪出去又丢了格挡”的错误。

不要在 Dodge 中手动移除 `Blocking` 或 `ParryWindow`，也不要直接调用 Guard 的 `BeginGuardExit()` 或 `EndAbility()`。这些 Loose Tag 和生命周期属于 `UFirstGA_DKGuardParry`；`CancelAbilities()` 会让它沿自己的取消/结束路径完成清理，由外部重复操作容易破坏 Tag 计数。

`Jump` 和 Shift 长按奔跑不是 GameplayAbility，所以上面的 `ActivationBlockedTags` 管不到它们。还要在 `ADKCharacter` 做下面两处小型门控，否则会出现“举盾起跳”或 Shift 把 `GuardMoveSpeed = 0` 覆盖成 250/500。

先在 `DKCharacter.h` 的 public 区域增加：

```cpp
	// 只根据当前 Shift 长按状态返回正常移动速度；Guard/Break/Execute 结束时使用。
	FORCEINLINE float GetDesiredLocomotionSpeed() const
	{
		return bIsRunning ? RunSpeed : WalkSpeed;
	}
```

把 `Input_JumpStarted` 改为：

```cpp
void ADKCharacter::Input_JumpStarted(const FInputActionValue& Value)
{
	if (FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing)))
	{
		return;
	}

	Jump();
}
```

输入回调可能在同一帧先执行 `Jump()`、随后才激活 Guard/GuardBreak/Execute，而 CharacterMovement 还没来得及消费 `bPressedJump`。所以门控只负责阻止新的 Jump；本册三个动作的 `ActivateAbility` 还会在成功进入后调用一次 `DK->StopJumping()`，清除同帧 pending jump，避免恢复移动时补跳。

把 `SetRunning` 改为：

```cpp
void ADKCharacter::SetRunning(bool bNewRunning)
{
	bIsRunning = bNewRunning;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// 输入状态仍要更新，但动作锁定期间不能覆盖 Guard/Break/Execute 的速度。
	const bool bActionLocksMoveSpeed = FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing));

	if (bActionLocksMoveSpeed)
	{
		return;
	}

	Movement->MaxWalkSpeed = GetDesiredLocomotionSpeed();
}
```

因此 Guard、GuardBreak 和 Execute 的 `EndAbility` 不应恢复进入动作时保存的旧 `MaxWalkSpeed`，而应写入 `DK->GetDesiredLocomotionSpeed()`。本册三个代码段均按这个规则处理：玩家在动作期间松开 Shift，结束后回 250；一直按住 Shift，结束后才回 500。

此时“格挡直接闪避”已经实现，但只放开了 `GuardParry → Dodge` 这一条有方向的取消关系。`GuardBreak`、`Executing`、换武器和死亡等限制仍由 Dodge 的 `ActivationBlockedTags` 保留，不要为了省事删除整组门控。

如果已经按《主角索敌（锁定、切换与解除）实现指导》接入锁敌，还要防止在格挡中切换“锁定/解锁”模式。原因是 Guard 和 TargetLock 都会暂存并修改：

```text
bOrientRotationToMovement
bUseControllerDesiredRotation
```

若格挡中途切换锁定模式，两套“进入前状态”会互相覆盖。最小修复是在 `ADKCharacter::Input_TargetLock` 开头增加：

```cpp
	if (FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing)))
	{
		return;
	}
```

同样在 `Input_SwitchTarget` 开头至少阻止 `DK.Status.Executing`。已经在格挡前锁定的目标可以继续保持，Guard 保存/恢复的正好是锁定移动设置；只是不要在 Guard 生命周期中途开关模式。

### 13.4 为什么破防能力不负责恢复到 20 精力

`20` 是“重新举剑所需最低精力”，不是破防结束时强制赠送的精力。破防期间恢复暂停 GE 仍可能存在；结束后按普通 20/秒恢复，达到 20 后 `CanActivateAbility` 才允许再次格挡。

这比破防结束瞬间直接 SetStamina(20) 更自然，也不会绕过 AttributeSet 和 UI。

### 编译闸门 6

先只编译 GuardParry 和 GuardBreak。此时还没有授予能力，运行行为仍应不变。编译通过后再制作输入和 Montage。

---

## 14. 主角输入、动画与数据资产配置

### 14.1 新建输入资产

在 `/Game/DKCharacter/Input/Action/` 新建：

| 资产 | Value Type | 推荐按键 |
|---|---|---|
| `IA_GuardParry` | Boolean | 鼠标右键 |
| `IA_Execute` | Boolean | E |

在 `IMC_Default` 中增加：

```text
IA_GuardParry → Right Mouse Button
IA_Execute    → E
```

在 `/Game/DKCharacter/DA_InputConfig` 的 `Ability Input Actions` 中增加：

```text
IA_GuardParry → InputTag.GuardParry
IA_Execute    → InputTag.Execute
```

不要放进 `Native Input Actions`。这两个输入都需要通过现有 ASC 的 `DynamicSpecSourceTags` 激活 Ability，并且 Guard 需要接收 Completed/Canceled 的松开边沿。

### 14.2 主角现有骨骼与可直接使用的动画

当前主角实际使用：

```text
Mesh     /Game/SwordAnimsetPro/Demo/Character/Mesh/SK_DKMannequin
Skeleton SK_Mannequin_Skeleton
AnimBP   /Game/DKCharacter/AnimBP/Sword/ABP_DK_Sword
Slot     DefaultSlot
```

以下 SwordAnimsetPro 动画与主角当前骨骼一致，可以直接使用：

```text
/Game/SwordAnimsetPro/Animations/In-Place/Block_In_Anim
/Game/SwordAnimsetPro/Animations/In-Place/Block_Hold_Anim
/Game/SwordAnimsetPro/Animations/In-Place/Block_Hit_Anim
/Game/SwordAnimsetPro/Animations/In-Place/Block_Out_Anim
```

创建 `AM_DK_GuardParry`，放到你现有主角 Montage 目录，`Slot` 选择 `DefaultSlot`。

推荐排列：

| Section | 动画段 |
|---|---|
| Start | `Block_In_Anim` |
| Loop | `Block_Hold_Anim` |
| Hit | `Block_Hit_Anim` |
| Parry | 重定向后的 `AS_Parry_L_Seq` |
| End | `Block_Out_Anim` |

Section 关联必须是：

```text
Start → Loop
Loop → Loop
Hit → Loop
Parry → Loop
End → 无
```

弹反有效时间由 Ability 中的 `WaitDelay(0.15)` 和 `DK.Status.ParryWindow` 管理。**不要**再在 Montage 中额外放一套 ParryWindow Notify，否则两套时序会互相打架。

### 14.3 弹反与破防动画重定向

Katana 动画的规范源路径是：

```text
/Game/Katana_Animations/Animations/Sequence/08_Hit/08_Block/
```

推荐：

```text
弹反：AS_Parry_L_Seq（以后可再加入 AS_Parry_R_Seq）
破防：AS_Block_Hit_Break_Seq
```

`08_Hit/07_Knock_Down` 下看到的同名文件是 Redirector，不要从那里复制。

使用现有：

```text
/Game/IK/RTG_Dodge
```

把源动画重定向到 `SK_DKMannequin / SK_Mannequin_Skeleton`。重定向后的资产建议放到：

```text
/Game/DKCharacter/Animations/Defense/
```

用重定向后的 `AS_Block_Hit_Break_Seq` 创建独立 `AM_DK_GuardBreak`，`Slot = DefaultSlot`。首版不推荐直接使用带明显 Root Motion 的 `Block_Breach_Anim`，否则破防能力禁用 CharacterMovement 的同时，动画根运动可能让角色发生意外位移。

### 14.4 为轻攻击开放“转弹反”取消窗口

代码中的 `CanActivateAbility` 会在攻击中检查：

```text
DK.Status.Action.Cancelable.By.Parry
```

当前四个主角轻攻击 Montage 的 `Action Cancel Window / ANS_ActionCancel` 只授予了 `.By.Dodge`。如果不完成这一步，平时可以举盾，但攻击过程中按右键永远不会启动 Guard/Parry。

不要凭文件名盲改可能未被使用的副本。`Default Weapon Abilities` 与 `Light Attack Montages` 是武器数据中的同级字段，不是“Ability 里面再嵌一个 Montage 数组”。请从：

```text
BP_DKWeapon_Sword
→ 类默认值
→ DKWeaponData / Weapon Data
→ Light Attack Montages
→ 逐个打开数组元素
```

旁边的 `Default Weapon Abilities` 只用于确认轻攻击输入仍映射到 `UFirstGA_DKLightAttack`，不要进入 Ability 类里寻找 Montage 数组。

逐个打开当前武器真正引用的攻击 Montage。对每个希望允许“收招转弹反”的 `Action Cancel Window`：

1. 选中 `ANS_ActionCancel`；
2. 找到 `Granted Cancel Tags`；
3. 保留现有 `DK.Status.Action.Cancelable.By.Dodge`；
4. 再增加 `DK.Status.Action.Cancelable.By.Parry`；
5. 不要把窗口铺满整段攻击，只放在你希望允许取消的收招区间。

首版建议四段轻攻击都沿用各自现有的 Dodge 取消窗口范围，只额外增加 `.By.Parry`，这样不会同时改动战斗节奏与窗口长度。

### 14.5 BP_DKCharacter 配置

打开 `BP_DKCharacter → 类默认值`：

```text
Combat | Defense | Anim
  Guard Parry Montage = AM_DK_GuardParry
  Guard Break Montage = AM_DK_GuardBreak
```

选中 `DKDefenseComponent`：

```text
Guard Half Angle Degrees = 60
Minimum Stamina To Guard = 20
Parry Window Duration = 0.15
Guard Move Speed = 0
Guard Break Duration = 1.3
```

### 14.6 授予 Guard 与 GuardBreak

打开 `/Game/DKCharacter/Weapons/BP_DKWeapon_Sword`，在武器数据的 `Default Weapon Abilities` 中增加：

```text
InputTag.GuardParry → UFirstGA_DKGuardParry
```

Guard 只在持剑时授予，符合 `CanActivateAbility` 的装备检查。

打开当前实际由主角引用的 `DA_DKStartUpData`：

- `Reactive Abilities` 增加 `UFirstGA_DKGuardBreak`；
- `Start Up Gameplay Effects` 增加 `UFirstGE_StaminaRegenGuarding`；
- 保留现有 `UFirstGE_StaminaRegen`，不要删除。

> 项目中存在过多个相似命名/位置的 `DA_DKStartUpData` 与 `DA_InputConfig`。不要只凭名字修改。以 `BP_DKCharacter → Character Start Up Data` 和 `Input Config Data Asset` 当前实际引用的资产为准。

### 14.7 第一轮主角侧验收

此时即使 BOSS 攻击尚未接入防御，也可以验证输入生命周期：

1. 装备剑；
2. 按住右键，应该播放 Start 后循环 Loop；
3. 松开右键，应该立即跳到 End 并结束；
4. 按下后的前 0.15 秒用 GAS Debugger 观察到 `DK.Status.ParryWindow`；
5. 持续按住时只有 `DK.Status.Blocking` 与 `DK.Status.Defending`；
6. 松开第一帧 `Blocking` 消失，`Defending` 到 End 播完才消失；
7. 未装备剑、精力低于 20、空中时，右键不能启动。
8. 在轻攻击 `.By.Parry` 窗口内按右键，攻击被取消并进入 Guard；窗口外按右键不应强行打断攻击。
9. Guard 中按空格不能起跳；Guard 中按下或松开 Shift 都不能覆盖 `GuardMoveSpeed`。
10. Guard 任一阶段按可用闪避会取消 Guard 并进入 Dodge；Dodge 费用或冷却检查失败时 Guard 必须保持。

如果按住后马上自动结束，检查 Montage 的 `Loop → Loop`；如果松开后不结束，先检查第 6.1 节的 InputReleased 修复，再检查 `End → 无`。

---

## 15. 把防御解析接到 BOSS 普通攻击与三连击

### 15.1 扩展 UFirstBossGameplayAbility

在 `FirstBossGameplayAbility.h` 顶部增加：

```cpp
#include "Types/FirstCombatTypes.h"
```

在 public 区域增加：

```cpp
	// 返回 Damaged 时派生攻击继续应用生命伤害；其它结果已经被消费。
	EFirstDefenseResult ResolveTargetDefense(
		AActor* TargetActor,
		const FFirstMeleeDefenseData& AttackData) const;
```

在 `FirstBossGameplayAbility.cpp` 顶部增加：

```cpp
#include "Components/Combat/DKDefenseComponent.h"
```

实现：

```cpp
EFirstDefenseResult UFirstBossGameplayAbility::ResolveTargetDefense(
	AActor* TargetActor,
	const FFirstMeleeDefenseData& AttackData) const
{
	if (!TargetActor)
	{
		return EFirstDefenseResult::Avoided;
	}

	// 只有拥有 DKDefenseComponent 的目标才进入玩家防御系统。
	// 训练假人、其它敌人或未来没有该组件的目标继续走原伤害链。
	if (UDKDefenseComponent* DefenseComponent =
		TargetActor->FindComponentByClass<UDKDefenseComponent>())
	{
		return DefenseComponent->ResolveIncomingMeleeAttack(
			GetAvatarActorFromActorInfo(),
			AttackData);
	}

	return EFirstDefenseResult::Damaged;
}
```

### 15.2 在 ABossCharacter 保存每种攻击的防御描述

在 `BossCharacter.h` 顶部增加：

```cpp
#include "Types/FirstCombatTypes.h"
```

在现有 `NormalAttackDamage`、`ThreeComboDamagePerHit` 附近增加：

```cpp
	// 普通攻击的防御白名单与资源伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense",
		meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData NormalAttackDefenseData;

	// 三连三段首版共用同一描述，但每个碰撞窗口会独立解析一次。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense",
		meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData ThreeComboDefenseData;

	FORCEINLINE const FFirstMeleeDefenseData& GetNormalAttackDefenseData() const
	{
		return NormalAttackDefenseData;
	}

	FORCEINLINE const FFirstMeleeDefenseData& GetThreeComboDefenseData() const
	{
		return ThreeComboDefenseData;
	}
```

在 `ABossCharacter::ABossCharacter()` 中显式填写白名单：

```cpp
	NormalAttackDefenseData.bBlockable = true;
	NormalAttackDefenseData.bParryable = true;
	NormalAttackDefenseData.GuardStaminaDamage = 25.f;
	NormalAttackDefenseData.ParryPoiseDamage = 50.f;

	ThreeComboDefenseData.bBlockable = true;
	ThreeComboDefenseData.bParryable = true;
	ThreeComboDefenseData.GuardStaminaDamage = 15.f;
	ThreeComboDefenseData.ParryPoiseDamage = 50.f;
```

编译后在 `BP_Boss → Boss | Combat | Defense` 中再次确认数值。C++ 默认值用于新蓝图实例，已有蓝图可能保存了旧的结构默认值；如果详情仍显示 `false/0`，点击属性右侧黄色“重置为默认值”箭头或手动填写。

### 15.3 修改普通攻击命中

把 `UGA_Boss_NormalAttack::HandleMeleeHit` 整体替换为：

```cpp
void UGA_Boss_NormalAttack::HandleMeleeHit(FGameplayEventData Payload)
{
	AActor* TargetActor = const_cast<AActor*>(Payload.Target.Get());
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!TargetActor || !Boss)
	{
		return;
	}

	const EFirstDefenseResult DefenseResult = ResolveTargetDefense(
		TargetActor,
		Boss->GetNormalAttackDefenseData());

	if (DefenseResult != EFirstDefenseResult::Damaged)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle =
		MakeBossDamageEffectSpecHandle(
			UFirstGE_Damage::StaticClass(),
			Boss->GetNormalAttackDamage());

	ApplyEffectSpecHandleToTarget(TargetActor, SpecHandle);
}
```

### 15.4 修改三连击每段命中

把 `UGA_Boss_ThreeCombo::HandleMeleeHit` 整体替换为：

```cpp
void UGA_Boss_ThreeCombo::HandleMeleeHit(FGameplayEventData Payload)
{
	AActor* TargetActor = const_cast<AActor*>(Payload.Target.Get());
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!TargetActor || !Boss)
	{
		return;
	}

	const EFirstDefenseResult DefenseResult = ResolveTargetDefense(
		TargetActor,
		Boss->GetThreeComboDefenseData());

	if (DefenseResult != EFirstDefenseResult::Damaged)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle =
		MakeBossDamageEffectSpecHandle(
			UFirstGE_Damage::StaticClass(),
			Boss->GetThreeComboDamagePerHit());

	ApplyEffectSpecHandleToTarget(TargetActor, SpecHandle);
}
```

三连的 `HandleMeleeHit` 会被三个武器碰撞窗口分别调用，不需要在 Ability 中手写 `CurrentHitIndex` 才能实现逐段格挡。

### 15.5 攻击取消时必须兜底关闭武器碰撞

弹反可能在 `FirstANS_WeaponCollision` 的 Notify State 中段取消 Montage。动画被取消时，`NotifyEnd` 并非所有异常路径都绝对可靠，因此普通攻击和三连都要覆写 `EndAbility`。

在两个攻击 Ability 的 `.h` protected 区域分别增加：

```cpp
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;
```

两个 `.cpp` 顶部确认包含：

```cpp
#include "Components/Combat/BossCombatComponent.h"
```

两个类分别实现同样的清理：

```cpp
void UGA_Boss_NormalAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UBossCombatComponent* Combat =
		GetBossCombatComponentFromActorInfo())
	{
		Combat->ToggleWeaponCollision(false);
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

三连类只把函数限定名改为：

```cpp
UGA_Boss_ThreeCombo::EndAbility
```

在两个构造函数中增加：

```cpp
	bIsCancelable = true;
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
```

### 15.6 三连 Montage 的三个碰撞窗口

打开 `AM_Boss_Attack_Combo`，必须看到三个彼此分开的 `FirstANS_WeaponCollision`：

```text
第一刀有效帧：Begin ─ End
第二刀有效帧：Begin ─ End
第三刀有效帧：Begin ─ End
```

每一段 End 都会调用现有：

```cpp
ToggleWeaponCollision(false)
→ OverlappedActors.Empty()
```

如果把整个三连只包在一个超长碰撞窗口里，同一主角只会进入 `OverlappedActors` 一次，后两段根本不会产生新的命中事件，也就无法做到逐段格挡。

### 15.7 未来投技如何调用

投技的攻击描述：

```cpp
FFirstMeleeDefenseData ThrowDefenseData;
// 保持默认 false / false / 0 / 0。

const EFirstDefenseResult Result =
	ResolveTargetDefense(TargetActor, ThrowDefenseData);
```

它一定返回 `Damaged`（除非目标死亡/无敌）。不要为了投技在防御组件中写特殊类名判断。

### 编译闸门 7

完整编译并只测试普通格挡：

- 正面挡普通攻击：生命不变，精力 -25；
- 正面挡三连：每段 -15；
- 背后攻击：正常扣血，不扣格挡精力；
- 挡到精力 0：该段不扣血，随后进入 1.3 秒破防；
- 破防后的下一段三连：正常扣血；
- 格挡命中后 0.75 秒内不恢复，之后按当前状态恢复；
- 格挡持续期间只按 2/秒恢复，而不是 22/秒。

此阶段 `Boss.Event.Parried` 尚未授予接收能力，所以先不要验收 BOSS 弹反表现。

本阶段测试“普通格挡”时，先按住右键超过 `0.15` 秒再让攻击命中。若命中发生在前 `0.15` 秒，防御组件会正确吞掉本次伤害并发送 `Boss.Event.Parried`，但第 17 节的接收 Ability 尚未授予，BOSS 暂时不会播放弹反反应；这不是格挡失效。完成第 17 节后再测试窗口内弹反。

---

## 16. ABossCharacter：弹反参数、处决点与五秒回韧

### 16.1 BossCharacter.h 新增内容

前置声明增加：

```cpp
class USceneComponent;
```

在 public 区域增加参数与接口：

```cpp
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Poise",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ParryStaggerDuration = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Poise",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float PoiseRecoveryDelay = 5.f;

	// 玩家在传送到 ExecutionPoint 之前发出请求；这是对原始站位的服务端容差，
	// 与 ExecutionPoint 距离 BOSS 多远无关。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Execution",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ExecutionRequestRange = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Poise",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ParryStaggerMontage;

	// 首版只使用 Start / Loop；当前代码不会跳转 Recover。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Execution",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutableMontage;

	// 与主角 Execution_01 配对的 Target_01 Montage。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Execution",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionTargetMontage;

	FORCEINLINE float GetParryStaggerDuration() const
	{
		return ParryStaggerDuration;
	}

	FORCEINLINE float GetPoiseRecoveryDelay() const
	{
		return PoiseRecoveryDelay;
	}

	FORCEINLINE float GetExecutionRequestRange() const
	{
		return ExecutionRequestRange;
	}

	FORCEINLINE UAnimMontage* GetParryStaggerMontage() const
	{
		return ParryStaggerMontage;
	}

	FORCEINLINE UAnimMontage* GetExecutableMontage() const
	{
		return ExecutableMontage;
	}

	FORCEINLINE UAnimMontage* GetExecutionTargetMontage() const
	{
		return ExecutionTargetMontage;
	}

	FTransform GetExecutionPointTransform() const;

	// 所有 Poise 改动都经过 GE，保留 Clamp 与 UI 广播。
	void ApplyPoiseDelta(float Delta);
	void RestartPoiseRecoveryTimer();
	void ClearPoiseRecoveryTimer();
	void RestorePoiseToFull();
```

在 private 区域增加：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boss|Execution",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> ExecutionPoint;

	FTimerHandle PoiseRecoveryTimerHandle;

	void HandlePoiseRecoveryExpired();
```

`BossCharacter.h` 顶部还要有：

```cpp
#include "TimerManager.h"
```

### 16.2 BossCharacter.cpp 创建处决锚点

顶部增加：

```cpp
#include "Components/SceneComponent.h"
#include "AbilitySystem/GameplayEffects/FirstGE_PoiseChange.h"
```

构造函数中增加：

```cpp
	ExecutionPoint =
		CreateDefaultSubobject<USceneComponent>(TEXT("ExecutionPoint"));
	ExecutionPoint->SetupAttachment(GetRootComponent());

	// 默认在 BOSS 正前方 120 单位，Yaw 180° 表示玩家面向 BOSS。
	ExecutionPoint->SetRelativeLocation(FVector(120.f, 0.f, 0.f));
	ExecutionPoint->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
```

它只是首版固定对齐点，不需要 Motion Warping。以后可以在 `BP_Boss` 组件视图中拖动它，让两套处决动画精确吻合。

### 16.3 BossCharacter.cpp 实现韧性工具

```cpp
FTransform ABossCharacter::GetExecutionPointTransform() const
{
	return ExecutionPoint
		? ExecutionPoint->GetComponentTransform()
		: GetActorTransform();
}

void ABossCharacter::ApplyPoiseDelta(float Delta)
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponent();
	if (!ASC || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	Context.AddInstigator(this, this);

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UFirstGE_PoiseChange::StaticClass(),
		1.f,
		Context);

	if (!Spec.IsValid())
	{
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(
		MyGameplayTags::Combat_SetByCaller_PoiseDelta,
		Delta);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

	if (Delta < 0.f)
	{
		RestartPoiseRecoveryTimer();
	}
}

void ABossCharacter::RestartPoiseRecoveryTimer()
{
	GetWorldTimerManager().ClearTimer(PoiseRecoveryTimerHandle);

	if (PoiseRecoveryDelay <= 0.f)
	{
		HandlePoiseRecoveryExpired();
		return;
	}

	GetWorldTimerManager().SetTimer(
		PoiseRecoveryTimerHandle,
		this,
		&ThisClass::HandlePoiseRecoveryExpired,
		PoiseRecoveryDelay,
		false);
}

void ABossCharacter::ClearPoiseRecoveryTimer()
{
	GetWorldTimerManager().ClearTimer(PoiseRecoveryTimerHandle);
}

void ABossCharacter::RestorePoiseToFull()
{
	if (!FirstAttributeSet)
	{
		return;
	}

	const float MissingPoise =
		FirstAttributeSet->GetMaxPoise() - FirstAttributeSet->GetPoise();

	if (MissingPoise > 0.f)
	{
		ApplyPoiseDelta(MissingPoise);
	}
}

void ABossCharacter::HandlePoiseRecoveryExpired()
{
	UFirstAbilitySystemComponent* ASC = GetFirstAbilitySystemComponent();
	if (!ASC || ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	// 正在配对处决时计时器本应已清除；这里再做一次安全保护。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted))
	{
		return;
	}

	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable))
	{
		FGameplayEventData ExpiredEvent;
		ExpiredEvent.Instigator = this;
		ExpiredEvent.Target = this;

		// GameplayEvent 同步通知 Executable Ability 退出，再恢复数值。
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this,
			MyGameplayTags::Boss_Event_ExecutableExpired,
			ExpiredEvent);
	}

	RestorePoiseToFull();
}
```

`ApplyPoiseDelta(-50)` 会自动重启五秒计时；正数恢复不会再次启动计时。

### 16.4 GE_Boss_Initialize

打开当前 `BP_Boss` 实际使用的初始化 GE，确认：

```text
Poise = 100
MaxPoise = 100
```

必须先设 MaxPoise 再确认 Poise，避免 Poise 在应用顺序中被旧最大值夹紧。

---

## 17. BOSS 普通弹反僵直：UGA_Boss_ParryStagger

这个能力不读取 `Boss.Status.HitReactWindow`，它与普通受击完全独立。

### 17.1 GA_Boss_ParryStagger.h

**创建类信息**

- 父类：`UFirstBossGameplayAbility`（进入“所有类”，搜索 `FirstBossGameplayAbility`）；
- 向导类名：`GA_Boss_ParryStagger`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/Abilities/BOSS`；
- 生成结果：`Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.h` 和对应的 `Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.cpp`。

新建：

`Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "GA_Boss_ParryStagger.generated.h"

UCLASS()
class FIRST_API UGA_Boss_ParryStagger : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_ParryStagger();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void HandleStaggerFinished();

	void FinishParryStagger(bool bWasCancelled);
};
```

### 17.2 GA_Boss_ParryStagger.cpp

新建：

`Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.cpp`

```cpp
#include "AbilitySystem/Abilities/BOSS/GA_Boss_ParryStagger.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_ParryStagger::UGA_Boss_ParryStagger()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_ParryStagger);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_Parried;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_ParryStagger::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC || !Boss->GetFirstAttributeSet())
	{
		FinishParryStagger(true);
		return;
	}

	// 先取消整套攻击。攻击 EndAbility 会再次兜底关闭碰撞。
	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	ASC->CancelAbilities(&AttackTags, nullptr, this);

	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		Combat->ToggleWeaponCollision(false);
	}

	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}

	const float PoiseDamage = TriggerEventData
		? FMath::Max(TriggerEventData->EventMagnitude, 0.f)
		: 50.f;

	Boss->ApplyPoiseDelta(-PoiseDamage);

	const float NewPoise = Boss->GetFirstAttributeSet()->GetPoise();
	if (NewPoise <= 0.f)
	{
		// 第二次 50→0：不再叠加普通 1 秒僵直，直接转入可处决。
		FGameplayEventData PoiseBrokenEvent;
		PoiseBrokenEvent.Instigator =
			TriggerEventData ? TriggerEventData->Instigator : nullptr;
		PoiseBrokenEvent.Target = Boss;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Boss,
			MyGameplayTags::Boss_Event_PoiseBroken,
			PoiseBrokenEvent);

		FinishParryStagger(false);
		return;
	}

	// 100→50：只播放普通弹反僵直。
	const float StaggerDuration = FMath::Max(
		Boss->GetParryStaggerDuration(),
		0.01f);

	if (UAnimMontage* Montage = Boss->GetParryStaggerMontage())
	{
		const float PlayRate = Montage->GetPlayLength() > 0.f
			? Montage->GetPlayLength() / StaggerDuration
			: 1.f;

		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				TEXT("BossParryStaggerMontage"),
				Montage,
				PlayRate);
		MontageTask->ReadyForActivation();
	}

	// 玩法状态严格由 1.0 秒 Delay 决定，不由动画原始长度决定。
	UAbilityTask_WaitDelay* DelayTask =
		UAbilityTask_WaitDelay::WaitDelay(this, StaggerDuration);
	DelayTask->OnFinish.AddDynamic(
		this,
		&ThisClass::HandleStaggerFinished);
	DelayTask->ReadyForActivation();
}

void UGA_Boss_ParryStagger::HandleStaggerFinished()
{
	FinishParryStagger(false);
}

void UGA_Boss_ParryStagger::FinishParryStagger(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}
```

### 17.3 这里绝对不要出现的判断

```cpp
if (NewPoise <= MaxPoise * 0.5f)
{
	// Big Stagger
}
```

旧 50% 大僵直已经取消。唯一分界是：

```text
NewPoise > 0  → 1 秒普通弹反僵直
NewPoise <= 0 → Boss.Event.PoiseBroken
```

### 17.4 授予与首轮测试

把 `UGA_Boss_ParryStagger` 加入 `DA_BossStartUpData → Reactive Abilities`。

暂时可把现有 `AM_Boss_Hit_Fwd` 填到 `BP_Boss → Parry Stagger Montage` 做逻辑测试，但最终推荐使用第 22 节的独立弹反僵直动画，避免普通受击和弹反共用同一视觉语义。

完整编译后测试：

```text
第一次成功弹反：
  攻击立即中断
  武器碰撞关闭
  Poise 100→50
  BOSS 停 1.0 秒
  不进入旧大僵直

等待 5 秒不再弹反：
  Poise 50→100

第二次弹反在 5 秒内成功：
  Poise 50→0
  发送 PoiseBroken
  不再播放普通 1 秒僵直
```

第二次弹反后的可处决表现要到下一节接入后才完整。

---

## 18. BOSS 可处决状态：UGA_Boss_Executable

第二次弹反到 0 后，这个能力长期持有 `Boss.Status.Executable + Boss.Status.Staggered`，直到超时或处决完成。

### 18.1 GA_Boss_Executable.h

**创建类信息**

- 父类：`UFirstBossGameplayAbility`（进入“所有类”，搜索 `FirstBossGameplayAbility`）；
- 向导类名：`GA_Boss_Executable`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/Abilities/BOSS`；
- 生成结果：`Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h` 和对应的 `Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.cpp`。

新建：

`Source/First/Public/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "GA_Boss_Executable.generated.h"

class ADKCharacter;

UCLASS()
class FIRST_API UGA_Boss_Executable : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_Executable();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleExecutionStarted(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionAborted(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionFinished(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionApplyDamage(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutableExpired(FGameplayEventData Payload);

	void StopBossMovement();
	void PlayExecutableLoop();
	void RemoveBeingExecutedTag();
	void NotifyExecutingPlayerAbort();
	void ReturnToExecutableAfterAbort();
	void HandleExecutionSafetyTimeout();
	void FinishExecutable(bool bWasCancelled);

	UPROPERTY()
	TWeakObjectPtr<ADKCharacter> ExecutingPlayer;

	bool bExecutionStarted = false;
	bool bExecutionDamageApplied = false;
	bool bBossKilledByExecution = false;
	bool bPlayerTerminalEventHandled = false;
	bool bOwnsBeingExecutedTag = false;
	FTimerHandle ExecutionSafetyTimerHandle;
};
```

### 18.2 GA_Boss_Executable.cpp

新建：

`Source/First/Private/AbilitySystem/Abilities/BOSS/GA_Boss_Executable.cpp`

```cpp
#include "AbilitySystem/Abilities/BOSS/GA_Boss_Executable.h"

#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_ExecutionDamage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/Combat/BossCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyGameplayTags.h"

UGA_Boss_Executable::UGA_Boss_Executable()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::Boss_Ability_Executable);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);

	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = MyGameplayTags::Boss_Event_PoiseBroken;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UGA_Boss_Executable::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	if (!Boss || !ASC)
	{
		FinishExecutable(true);
		return;
	}

	bExecutionStarted = false;
	bExecutionDamageApplied = false;
	bBossKilledByExecution = false;
	bPlayerTerminalEventHandled = false;
	bOwnsBeingExecutedTag = false;
	ExecutingPlayer.Reset();

	// 当前弹反路径已经在 ParryStagger 中取消攻击；这里再兜底一次，
	// 这样未来普通削韧来源发送 PoiseBroken 时也不会与攻击重叠。
	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MyGameplayTags::Boss_Ability_Attack);
	ASC->CancelAbilities(&AttackTags, nullptr, this);

	if (UBossCombatComponent* Combat = Boss->GetBossCombatComponent())
	{
		Combat->ToggleWeaponCollision(false);
	}

	StopBossMovement();
	PlayExecutableLoop();

	// false = 不只监听一次。处决启动后若配置错误而中止，玩家可以在剩余窗口重试。
	UAbilityTask_WaitGameplayEvent* StartTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionStarted,
			nullptr,
			false,
			true);
	StartTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleExecutionStarted);
	StartTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* AbortTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionAborted,
			nullptr,
			false,
			true);
	AbortTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleExecutionAborted);
	AbortTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* FinishedTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionFinished,
			nullptr,
			false,
			true);
	FinishedTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleExecutionFinished);
	FinishedTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* DamageTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutionApplyDamage,
			nullptr,
			false,
			true);
	DamageTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleExecutionApplyDamage);
	DamageTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ExpiredTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::Boss_Event_ExecutableExpired,
			nullptr,
			false,
			true);
	ExpiredTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleExecutableExpired);
	ExpiredTask->ReadyForActivation();
}

void UGA_Boss_Executable::StopBossMovement()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (!Boss)
	{
		return;
	}

	Boss->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AIController = Cast<AAIController>(Boss->GetController()))
	{
		AIController->StopMovement();
	}
}

void UGA_Boss_Executable::PlayExecutableLoop()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UAnimMontage* Montage = Boss ? Boss->GetExecutableMontage() : nullptr;
	UAnimInstance* AnimInstance = Boss && Boss->GetMesh()
		? Boss->GetMesh()->GetAnimInstance()
		: nullptr;

	if (!AnimInstance || !Montage)
	{
		return;
	}

	AnimInstance->Montage_Play(Montage);
	if (Montage->GetSectionIndex(TEXT("Start")) != INDEX_NONE)
	{
		AnimInstance->Montage_JumpToSection(TEXT("Start"), Montage);
	}
}

void UGA_Boss_Executable::HandleExecutionStarted(FGameplayEventData Payload)
{
	if (bExecutionStarted || bExecutionDamageApplied)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = Cast<ADKCharacter>(
		const_cast<AActor*>(Payload.Instigator.Get()));
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	UAnimMontage* TargetMontage = Boss
		? Boss->GetExecutionTargetMontage()
		: nullptr;

	if (!Boss || !Player || !ASC || !TargetMontage)
	{
		return;
	}

	// 玩家还没有传送到 ExecutionPoint；这里校验的是请求发生时的原始距离。
	// 它与 ExecutionPoint 的本地 X/Y/Z 偏移无关。
	if (FVector::DistSquared2D(Boss->GetActorLocation(), Player->GetActorLocation()) >
		FMath::Square(FMath::Max(Boss->GetExecutionRequestRange(), 0.f)))
	{
		return;
	}

	UAnimInstance* AnimInstance = Boss->GetMesh()
		? Boss->GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance)
	{
		return;
	}

	if (UAnimMontage* WeakMontage = Boss->GetExecutableMontage())
	{
		AnimInstance->Montage_Stop(0.1f, WeakMontage);
	}

	const float TargetPlaybackDuration =
		AnimInstance->Montage_Play(TargetMontage);
	if (TargetPlaybackDuration <= 0.f)
	{
		// 骨骼、Slot 或 Montage 配置错误时不发送确认，并恢复弱点 Loop。
		PlayExecutableLoop();
		return;
	}

	// 只有 Target Montage 真正开始播放后才添加确认 Tag。
	// 玩家 Ability 会在 SendGameplayEventToActor 返回后同步检查它。
	bExecutionStarted = true;
	bPlayerTerminalEventHandled = false;
	ExecutingPlayer = Player;
	Boss->ClearPoiseRecoveryTimer();
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted);
	bOwnsBeingExecutedTag = true;
	StopBossMovement();

	// 玩家 Ability/Actor 异常销毁、终止事件丢失时的最后保险。
	Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	Boss->GetWorldTimerManager().SetTimer(
		ExecutionSafetyTimerHandle,
		this,
		&ThisClass::HandleExecutionSafetyTimeout,
		FMath::Max(TargetPlaybackDuration + 0.5f, 0.5f),
		false);
}

void UGA_Boss_Executable::HandleExecutionAborted(FGameplayEventData Payload)
{
	if (!bExecutionStarted)
	{
		return;
	}

	// 这是玩家主动发来的终止事件；BOSS 结束时不要再回发一次 Abort。
	bPlayerTerminalEventHandled = true;

	// 伤害帧之后无论 BOSS 是否死亡，本次处决都已经消费。
	// 此时直接结束，不能退回 Executable 让玩家重复结算同一次处决伤害。
	if (bExecutionDamageApplied)
	{
		FinishExecutable(true);
		return;
	}

	ReturnToExecutableAfterAbort();
}

void UGA_Boss_Executable::HandleExecutionFinished(FGameplayEventData Payload)
{
	if (!bExecutionStarted)
	{
		return;
	}

	// 玩家 Montage 已经进入自己的清理流程。
	bPlayerTerminalEventHandled = true;

	if (ABossCharacter* Boss = GetBossCharacterFromActorInfo())
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}

	if (!bExecutionDamageApplied)
	{
		// 配对动画结束却没有收到 ApplyDamage，说明 Notify 漏配；
		// 回到可处决，而不是无伤结束或错误退出破韧状态。
		UE_LOG(LogTemp, Error,
			TEXT("Boss execution finished without Boss.Event.Execution.ApplyDamage"));
		ReturnToExecutableAfterAbort();
		return;
	}

	FinishExecutable(false);
}

void UGA_Boss_Executable::HandleExecutionApplyDamage(FGameplayEventData Payload)
{
	if (!bExecutionStarted || bExecutionDamageApplied)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = ExecutingPlayer.Get();
	UFirstAbilitySystemComponent* BossASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	UFirstAbilitySystemComponent* PlayerASC = Player
		? Player->GetFirstAbilitySystemComponent()
		: nullptr;
	UFirstAttributeSet* BossAttributes = Boss
		? Boss->GetFirstAttributeSet()
		: nullptr;

	if (!Boss || !Player || !BossASC || !PlayerASC || !BossAttributes)
	{
		return;
	}

	Boss->ClearPoiseRecoveryTimer();

	const float PlayerAttackPower = PlayerASC->GetNumericAttribute(
		UFirstAttributeSet::GetAttackPowerAttribute());
	const float ExecutionDamage =
		FMath::Max(PlayerAttackPower, 0.f) *
		FMath::Max(Player->GetExecutionDamageMultiplier(), 0.f);

	if (!FMath::IsFinite(ExecutionDamage) ||
		ExecutionDamage <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Execution damage is invalid. AttackPower=%.2f Multiplier=%.2f"),
			PlayerAttackPower,
			Player->GetExecutionDamageMultiplier());
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	FGameplayEffectContextHandle Context = PlayerASC->MakeEffectContext();
	Context.AddSourceObject(Player);
	Context.AddInstigator(Player, Player);

	FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
		UFirstGE_ExecutionDamage::StaticClass(),
		1.f,
		Context);

	if (!Spec.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("Failed to create execution damage GameplayEffect spec"));
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	Spec.Data->SetSetByCallerMagnitude(
		MyGameplayTags::Combat_SetByCaller_ExecutionDamage,
		ExecutionDamage);

	// AttributeSet 添加 Dead 与事件 Ability 激活是同步链路。
	// 因此必须在应用伤害前临时添加；若 BOSS 存活，结算后立即移除。
	const bool bAddedExecutedTag =
		!BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executed);
	if (bAddedExecutedTag)
	{
		BossASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_Executed);
	}

	const float HealthBefore = BossAttributes->GetHealth();
	PlayerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), BossASC);
	const float HealthAfter = BossAttributes->GetHealth();
	const bool bBossDead = BossASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);

	// 非致死处决不能遗留 Executed；否则以后普通攻击致死也会误走处决死亡分支。
	if (!bBossDead && bAddedExecutedTag)
	{
		BossASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_Executed);
	}

	// Instant GE 的返回 Handle 不能证明数值真的生效；用 Health 前后值验证。
	if (!bBossDead && HealthAfter >= HealthBefore - KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Execution damage effect applied but Boss Health did not decrease"));
		NotifyExecutingPlayerAbort();
		ReturnToExecutableAfterAbort();
		return;
	}

	bExecutionDamageApplied = true;
	bBossKilledByExecution = bBossDead;
}

void UGA_Boss_Executable::HandleExecutableExpired(FGameplayEventData Payload)
{
	if (bExecutionStarted)
	{
		return;
	}

	FinishExecutable(false);
}

void UGA_Boss_Executable::RemoveBeingExecutedTag()
{
	if (!bOwnsBeingExecutedTag)
	{
		return;
	}

	if (UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted);
	}

	bOwnsBeingExecutedTag = false;
}

void UGA_Boss_Executable::NotifyExecutingPlayerAbort()
{
	if (bPlayerTerminalEventHandled)
	{
		return;
	}

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	ADKCharacter* Player = ExecutingPlayer.Get();
	if (!Boss || !Player)
	{
		return;
	}

	FGameplayEventData AbortEvent;
	AbortEvent.Instigator = Boss;
	AbortEvent.Target = Player;

	// 发送前先置位，防止同步 GameplayEvent 回调造成重复通知。
	bPlayerTerminalEventHandled = true;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Player,
		MyGameplayTags::DK_Event_ExecutionAbortedByBoss,
		AbortEvent);
}

void UGA_Boss_Executable::ReturnToExecutableAfterAbort()
{
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);
	}
	RemoveBeingExecutedTag();

	bExecutionStarted = false;
	bExecutionDamageApplied = false;
	bBossKilledByExecution = false;
	bPlayerTerminalEventHandled = false;
	ExecutingPlayer.Reset();

	StopBossMovement();
	PlayExecutableLoop();

	if (Boss)
	{
		// 重新给玩家完整 5 秒，而不是沿用中止前已经消耗的时间。
		Boss->RestartPoiseRecoveryTimer();
	}
}

void UGA_Boss_Executable::HandleExecutionSafetyTimeout()
{
	if (!bExecutionStarted)
	{
		return;
	}

	if (bExecutionDamageApplied)
	{
		// 伤害已结算但玩家终止事件缺失：同时解除玩家侧动作，
		// 再由 EndAbility 按 BOSS 是否被这次伤害击杀做最终清理。
		NotifyExecutingPlayerAbort();
		FinishExecutable(false);
		return;
	}

	UE_LOG(LogTemp, Error,
		TEXT("Boss execution timed out before ApplyDamage/terminal event"));
	NotifyExecutingPlayerAbort();
	ReturnToExecutableAfterAbort();
}

void UGA_Boss_Executable::FinishExecutable(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

void UGA_Boss_Executable::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	const bool bMustReleaseExecutingPlayer =
		bExecutionStarted && !bPlayerTerminalEventHandled;

	if (bMustReleaseExecutingPlayer)
	{
		// 覆盖外部取消，以及非致死伤害后又被其它伤害杀死的竞态。
		NotifyExecutingPlayerAbort();
	}

	RemoveBeingExecutedTag();

	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	UFirstAbilitySystemComponent* ASC =
		GetFirstAbilitySystemComponentFromActorInfo();
	const bool bBossDead = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Shared_Status_Dead);
	UAnimInstance* AnimInstance = Boss && Boss->GetMesh()
		? Boss->GetMesh()->GetAnimInstance()
		: nullptr;

	// 只有“本次处决伤害确实致死”才保留 Target Montage 最后一帧。
	// BOSS 存活，或稍后被其它伤害击杀，都不能继续占用处决定格。
	if (AnimInstance && Boss && !bBossKilledByExecution)
	{
		if (UAnimMontage* WeakMontage = Boss->GetExecutableMontage())
		{
			AnimInstance->Montage_Stop(0.15f, WeakMontage);
		}
		if (UAnimMontage* TargetMontage = Boss->GetExecutionTargetMontage())
		{
			AnimInstance->Montage_Stop(0.15f, TargetMontage);
		}
	}

	if (Boss)
	{
		Boss->GetWorldTimerManager().ClearTimer(ExecutionSafetyTimerHandle);

		if (!bBossDead)
		{
			// 只要 Executable Ability 要结束而 BOSS 仍活着，就必须回满韧性。
			// 这同时覆盖非致死处决、外部取消、超时和伤害前异常中止。
			// 正常超时路径随后即使再调用一次也只是 MissingPoise=0，无副作用。
			Boss->RestorePoiseToFull();
		}
	}

	// bBossKilledByExecution 为 true 时不停止 Target Montage；
	// 其最后一帧作为处决致死后的死亡姿势保留。
	ExecutingPlayer.Reset();

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

这里的三个状态不能混成一个：

- `bExecutionDamageApplied` 表示本次处决已经结算过，负责阻止重复 Notify 和重复处决；
- `bBossKilledByExecution` 只表示这次伤害确实把 BOSS 打到 0，负责决定是否保留 Target Montage 的死亡定格。
- `bPlayerTerminalEventHandled` 表示玩家已经发来完成/中止事件，或 BOSS 已通知玩家中止；它避免双方反复回发终止事件，并保证 BOSS 被外部取消时玩家能立刻解除锁定。

伤害 Spec 由玩家 ASC 创建、应用到 BOSS ASC，因此来源身份也是玩家；伤害数值仍直接写 `DamageTaken`，不会进入普通攻防公式。非致死不是失败：结束时停止 Target Montage、回满韧性并让行为树恢复即可。

### 18.3 为什么弱点待机不用 PlayMontageAndWait

`Executable` 能力内部有两个阶段：

```text
弱点 Start/Loop
→ 收到 ExecutionStarted
→ 播放配对 Target Montage
```

如果弱点 Loop 使用 `PlayMontageAndWait` 并把 `OnInterrupted` 绑定到结束能力，第二个 Montage 正常覆盖第一个时会被误认为“可处决能力被打断”。所以弱点表现直接通过 `AnimInstance` 播放，Ability 生命周期由 GameplayEvent 管理。

### 18.4 授予能力

把 `UGA_Boss_Executable` 加入：

```text
DA_BossStartUpData → Reactive Abilities
```

此时第二次弹反后即使还没有玩家 Execute Ability，BOSS 也应：

- 停止行为树移动；
- 保持可处决弱点姿势；
- 5 秒后退出、Poise 回到 100；
- 不会在 50 点时进入这个状态。

---

## 19. 玩家按 E 处决：UFirstGA_DKExecute

首版不依赖索敌组件。按 E 时在 200 的二维距离内寻找最近的 `Boss.Status.Executable`，这样即使锁敌功能尚未完成，也能独立验收处决。

这个最小搜索没有视线检测，也没有通用安全落点查询，只适用于**无遮挡、平坦、开阔的 BOSS 战场**。墙后、跨楼层、台阶边与狭窄角落不是首版支持场景；第 22.5 节会说明固定 `ExecutionPoint` 的场地验收。不要把它误当成已经支持任意关卡几何的通用处决搜索。

### 19.1 FirstGA_DKExecute.h

**创建类信息**

- 父类：`UFirstDKGameplayAbility`（进入“所有类”，搜索 `FirstDKGameplayAbility`）；
- 向导类名：`FirstGA_DKExecute`；
- 类类型：`Public`；
- Path：`Source/First/Public/AbilitySystem/Abilities/DK`；
- 生成结果：`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKExecute.h` 和对应的 `Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKExecute.cpp`。

新建：

`Source/First/Public/AbilitySystem/Abilities/DK/FirstGA_DKExecute.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKExecute.generated.h"

class ABossCharacter;

UCLASS()
class FIRST_API UFirstGA_DKExecute : public UFirstDKGameplayAbility
{
	GENERATED_BODY()

public:
	UFirstGA_DKExecute();

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	ABossCharacter* FindExecutableBoss(
		const FGameplayAbilityActorInfo* ActorInfo) const;

	UFUNCTION()
	void HandleExecutionCompleted();

	UFUNCTION()
	void HandleExecutionInterrupted();

	UFUNCTION()
	void HandleBossExecutionAborted(FGameplayEventData Payload);

	void SendBossEvent(const FGameplayTag& EventTag);
	void FinishExecution(bool bWasCancelled);

	UPROPERTY(EditDefaultsOnly, Category="Execution", meta=(ClampMin="0.0"))
	float ExecutionRange = 200.f;

	UPROPERTY()
	TWeakObjectPtr<ABossCharacter> TargetBoss;

	bool bTerminalEventSent = false;
	bool bSavedMovementMode = false;
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	uint8 SavedCustomMovementMode = 0;

	bool bSavedPlayerPawnResponse = false;
	bool bSavedBossPawnResponse = false;
	TEnumAsByte<ECollisionResponse> SavedPlayerPawnResponse = ECR_Block;
	TEnumAsByte<ECollisionResponse> SavedBossPawnResponse = ECR_Block;
};
```

### 19.2 FirstGA_DKExecute.cpp

新建：

`Source/First/Private/AbilitySystem/Abilities/DK/FirstGA_DKExecute.cpp`

```cpp
#include "AbilitySystem/Abilities/DK/FirstGA_DKExecute.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Character/BossCharacter.h"
#include "Character/DKCharacter.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"

UFirstGA_DKExecute::UFirstGA_DKExecute()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MyGameplayTags::DK_Ability_Execute);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationOwnedTags.AddTag(MyGameplayTags::DK_Status_Invincible);

	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Attacking);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Dodging);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Defending);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
	ActivationBlockedTags.AddTag(MyGameplayTags::DK_Status_Executing);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
}

ABossCharacter* UFirstGA_DKExecute::FindExecutableBoss(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World)
	{
		return nullptr;
	}

	ABossCharacter* BestBoss = nullptr;
	float BestDistanceSquared = FMath::Square(ExecutionRange);

	for (TActorIterator<ABossCharacter> It(World); It; ++It)
	{
		ABossCharacter* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		UAbilitySystemComponent* BossASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
		if (!BossASC ||
			BossASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead) ||
			!BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Executable) ||
			BossASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_BeingExecuted))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(
			Avatar->GetActorLocation(),
			Candidate->GetActorLocation());

		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestBoss = Candidate;
		}
	}

	return BestBoss;
}

bool UFirstGA_DKExecute::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle,
		ActorInfo,
		SourceTags,
		TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const ADKCharacter* DK = ActorInfo
		? Cast<ADKCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;

	return DK &&
		DK->GetCharacterMovement()->IsMovingOnGround() &&
		FindExecutableBoss(ActorInfo) != nullptr;
}

void UFirstGA_DKExecute::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = FindExecutableBoss(ActorInfo);

	// 在改变位置/移动前先验证双方动画，避免缺资产时把角色锁死。
	if (!DK || !Boss ||
		!DK->GetExecutionMontage() ||
		!Boss->GetExecutionTargetMontage())
	{
		FinishExecution(true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		FinishExecution(true);
		return;
	}

	// CanActivate 检查和移动 Tick 之间可能仍残留同帧 Jump 请求。
	DK->StopJumping();

	TargetBoss = Boss;
	bTerminalEventSent = false;

	// BOSS Ability 若被外部死亡/强制取消，主动结束玩家侧锁移动和无敌。
	UAbilityTask_WaitGameplayEvent* BossAbortTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this,
			MyGameplayTags::DK_Event_ExecutionAbortedByBoss,
			nullptr,
			false,
			true);
	BossAbortTask->EventReceived.AddDynamic(
		this,
		&ThisClass::HandleBossExecutionAborted);
	BossAbortTask->ReadyForActivation();

	// 先用玩家原始站位请求 BOSS 接受处决；BOSS 只有在 Target Montage
	// 真正开始播放后才会同步添加 BeingExecuted 作为确认。
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionStarted);

	UAbilitySystemComponent* BossASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
	if (!BossASC || !BossASC->HasMatchingGameplayTag(
		MyGameplayTags::Boss_Status_BeingExecuted))
	{
		// 请求被拒绝时尚未改位置、移动或碰撞，直接结束不会产生免费瞬移。
		// BOSS 从未确认启动，不需要再回发一条 ExecutionAborted。
		bTerminalEventSent = true;
		FinishExecution(true);
		return;
	}

	if (UCharacterMovementComponent* Movement = DK->GetCharacterMovement())
	{
		SavedMovementMode = Movement->MovementMode;
		SavedCustomMovementMode = Movement->CustomMovementMode;
		bSavedMovementMode = true;

		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	UCapsuleComponent* PlayerCapsule = DK->GetCapsuleComponent();
	UCapsuleComponent* BossCapsule = Boss->GetCapsuleComponent();
	if (PlayerCapsule)
	{
		SavedPlayerPawnResponse =
			PlayerCapsule->GetCollisionResponseToChannel(ECC_Pawn);
		bSavedPlayerPawnResponse = true;
		PlayerCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (BossCapsule)
	{
		SavedBossPawnResponse =
			BossCapsule->GetCollisionResponseToChannel(ECC_Pawn);
		bSavedBossPawnResponse = true;
		BossCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	const FTransform ExecutionTransform = Boss->GetExecutionPointTransform();
	DK->SetActorLocationAndRotation(
		ExecutionTransform.GetLocation(),
		ExecutionTransform.Rotator(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			TEXT("DKExecutionMontage"),
			DK->GetExecutionMontage());

	MontageTask->OnCompleted.AddDynamic(
		this,
		&ThisClass::HandleExecutionCompleted);
	MontageTask->OnInterrupted.AddDynamic(
		this,
		&ThisClass::HandleExecutionInterrupted);
	MontageTask->OnCancelled.AddDynamic(
		this,
		&ThisClass::HandleExecutionInterrupted);
	MontageTask->ReadyForActivation();
}

void UFirstGA_DKExecute::SendBossEvent(const FGameplayTag& EventTag)
{
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = TargetBoss.Get();
	if (!DK || !Boss || !EventTag.IsValid())
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.Instigator = DK;
	EventData.Target = Boss;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Boss,
		EventTag,
		EventData);
}

void UFirstGA_DKExecute::HandleExecutionCompleted()
{
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionFinished);
	bTerminalEventSent = true;
	FinishExecution(false);
}

void UFirstGA_DKExecute::HandleExecutionInterrupted()
{
	SendBossEvent(MyGameplayTags::Boss_Event_ExecutionAborted);
	bTerminalEventSent = true;
	FinishExecution(true);
}

void UFirstGA_DKExecute::HandleBossExecutionAborted(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}

	// BOSS 已在结束自己的状态；不要再把 Aborted 事件回发给它。
	bTerminalEventSent = true;
	FinishExecution(true);
}

void UFirstGA_DKExecute::FinishExecution(bool bWasCancelled)
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		true,
		bWasCancelled);
}

void UFirstGA_DKExecute::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 外部路径直接取消能力时，也必须通知 BOSS 退出 BeingExecuted。
	if (!bTerminalEventSent && TargetBoss.IsValid())
	{
		SendBossEvent(MyGameplayTags::Boss_Event_ExecutionAborted);
		bTerminalEventSent = true;
	}

	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	ABossCharacter* Boss = TargetBoss.Get();

	if (DK)
	{
		if (UCapsuleComponent* PlayerCapsule = DK->GetCapsuleComponent();
			PlayerCapsule && bSavedPlayerPawnResponse)
		{
			PlayerCapsule->SetCollisionResponseToChannel(
				ECC_Pawn,
				SavedPlayerPawnResponse);
		}

		UAbilitySystemComponent* ASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(DK);
		const bool bPlayerDead = ASC && ASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

		if (!bPlayerDead && bSavedMovementMode)
		{
			UCharacterMovementComponent* Movement = DK->GetCharacterMovement();
			Movement->MaxWalkSpeed = DK->GetDesiredLocomotionSpeed();
			Movement->SetMovementMode(
				SavedMovementMode,
				SavedCustomMovementMode);
		}
	}

	if (Boss)
	{
		UAbilitySystemComponent* BossASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
		const bool bBossDead = BossASC && BossASC->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead);

		if (UCapsuleComponent* BossCapsule = Boss->GetCapsuleComponent();
			BossCapsule && bSavedBossPawnResponse)
		{
			if (bBossDead)
			{
				BossCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			}
			else
			{
				BossCapsule->SetCollisionResponseToChannel(
					ECC_Pawn,
					SavedBossPawnResponse);
			}
		}
	}

	bSavedMovementMode = false;
	bSavedPlayerPawnResponse = false;
	bSavedBossPawnResponse = false;
	TargetBoss.Reset();

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
```

### 19.3 授予 Execute

回到 `/Game/DKCharacter/Weapons/BP_DKWeapon_Sword` 的 `Default Weapon Abilities`，增加：

```text
InputTag.Execute → UFirstGA_DKExecute
```

因此未装备剑时 E 不会找到对应 AbilitySpec，也不会启动处决。

---

## 20. 处决伤害帧与存活/死亡分流

### 20.1 在 BOSS Target Montage 放精确伤害 Notify

项目已经有可复用的：

```text
UAnimNotify_DKGameplayEvent
编辑器显示名：DK Gameplay Event
```

它会把选定 GameplayTag 发送给 Montage 所属 Actor。把它放在 BOSS 的 `AM_Boss_ExecutionTarget_01` 真正被刺中/斩中的那一帧：

```text
Event Tag = Boss.Event.Execution.ApplyDamage
Montage Tick Type = Branching Point
```

不要把 Notify 放在最后一帧或 Blend Out 边界；要放在玩家 Montage 结束之前，并让 Trigger Weight 保持可触发。Branching Point 会让精确伤害帧的触发比普通排队 Notify 更确定。

不要把伤害写在玩家 Montage 的任意百分比计时器里。BOSS Target 动画上的 Notify 与被处决者姿势完全对齐。这个 Notify **只负责伤害帧**；不要再额外添加“ExecutionFinished Notify”，结束仍由玩家 Montage 的 `OnCompleted` 发送。

### 20.2 Target Montage 同时支持存活与死亡

在 `AM_Boss_ExecutionTarget_01` 资产详情中：

```text
Enable Auto Blend Out = 关闭
Slot = DefaultSlot
```

仍然关闭自动混出，是为了让**被处决伤害击杀**的 BOSS 保持 Target 动画最后一帧，避免尸体重新站起来。若 BOSS 存活，`GA_Boss_Executable::EndAbility` 会显式 `Montage_Stop(0.15f, TargetMontage)`，所以它会正常混回 locomotion，不会永久定格。

### 20.3 修改 GA_Boss_Death，避免覆盖处决演出

当前 `GA_Boss_Death` 一激活就 `CancelAllAbilities(this)`，随后播放通用 `DeathMontage`。如果处决伤害恰好致死并直接进入这条分支，它会立刻取消 `GA_Boss_Executable`，再用普通死亡动画覆盖配对动画。非致死处决不会激活 Death Ability，也不走本节特殊分支。

在 `ActivateAbility` 取得 Boss 后，先保存：

```cpp
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const bool bKilledByExecution = ASC && ASC->HasMatchingGameplayTag(
		MyGameplayTags::Boss_Status_Executed);
```

把原来的“取消所有其它能力”改为：

```cpp
	// 普通死亡取消其它能力；处决死亡保留正在播放的配对 Target Montage。
	if (ASC && !bKilledByExecution)
	{
		ASC->CancelAllAbilities(this);
	}
```

保留角色和 AI 的停止移动代码。停止移动后、播放普通死亡 Montage 前增加：

```cpp
	if (bKilledByExecution)
	{
		// Target Montage 自己包含死亡演出，并关闭了 Auto Blend Out。
		// 这里不再播放通用 DeathMontage，也不取消 Executable Ability。
		FinishDeath(false);
		return;
	}
```

处决伤害结算因此分成两条：

```text
Target Montage 的 ApplyDamage Notify
→ FirstGE_ExecutionDamage 写入 AttackPower × 10 到 DamageTaken
├─ Health > 0
│  ├─ 立即移除临时 Boss.Status.Executed
│  └─ Montage 完成后停止 Target、回满 Poise、恢复碰撞与 AI
└─ Health == 0
   ├─ AttributeSet 添加 Shared.Status.Dead
   ├─ GA_Boss_Death 看到 Boss.Status.Executed
   ├─ 不取消配对 Ability、不播放通用 DeathMontage
   └─ Target Montage 保持最后死亡姿势
```

`Boss.Status.Executed` 必须在应用伤害前临时加入，因为 `DamageTaken → Dead → GA_Boss_Death` 是同步链路；但 BOSS 存活时必须立刻移除。这样以后再被普通攻击打死，仍完整走原有通用 `DeathMontage`。

处决开始时双方胶囊已经临时把 `Pawn` 响应改为 `Ignore`。玩家 Execute 的 `EndAbility` 只在 BOSS 仍活着时恢复其旧响应；若已经出现 `Shared.Status.Dead`，则继续保持 `ECR_Ignore`，避免处决尸体在动画结束后重新阻挡或推挤主角。地面/墙体碰撞仍由其它通道保留。

### 20.4 处决配置错误时的安全行为

如果玩家/BOSS 配对动画播放结束但 BOSS Target Montage 上没有 `ApplyDamage` Notify：

- 输出红色 Error 日志；
- 不把 BOSS 假装成尸体；
- BOSS 返回 Executable Loop；
- 五秒窗口重新开始；
- 玩家可以修正 Notify 后再次测试。

这比“动画播完无条件扣血”更容易定位资产配置遗漏。注意：**正常扣了 `AttackPower × 10` 但 BOSS 没死不是配置错误**，只说明它的剩余生命高于本次处决伤害。

---

## 21. 行为树只接一个 bIsBusy

### 21.1 修改 C++ 默认容器

在 `UBTService_UpdateBossState::UBTService_UpdateBossState()` 中，现有 Attacking、DrawSword 后增加：

```cpp
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_Executable);
	ActionBlockingTags.AddTag(MyGameplayTags::Boss_Status_BeingExecuted);
```

严格来说，`GA_Boss_Executable` 已同时拥有 `Boss.Status.Staggered`，但显式加入另外两个 Tag 可以让服务的意图更清楚，也能抵御以后某个能力调整父状态时的遗漏。

### 21.2 必须手动检查 BT_Boss 中已经放置的服务节点

行为树里已经存在的 Service 节点会序列化自己的 `ActionBlockingTags`。修改 C++ 构造默认值不一定覆盖旧实例。

打开 `BT_Boss`：

1. 选中根战斗分支上现有的 `Update Boss State` 服务；
2. 在详情面板展开 `Action Blocking Tags`；
3. 确认至少包含：

```text
Boss.Status.Attacking
Boss.Status.DrawSword
Boss.Status.Staggered
Boss.Status.Executable
Boss.Status.BeingExecuted
```

### 21.3 bIsBusy 装饰器

攻击、后退、周旋、靠近等会发起动作的战斗分支，应该继续要求：

```text
bIsBusy == false
Observer Aborts = Both（观察期中止：双方）
```

这样标签一出现，正在执行的 `MoveTo` 会被行为树中止；标签消失后 Selector 再重新评估。

仍然不能只依赖下一次 Service Tick，所以两个 BOSS 能力激活时都显式调用：

```cpp
CharacterMovement->StopMovementImmediately();
AIController->StopMovement();
```

两层职责分别是：

```text
即时 StopMovement → 当帧停住，不再滑半步
bIsBusy            → 阻止行为树下一帧重新下达 MoveTo
```

### 21.4 不新增黑板键

不要新增：

```text
bWasParried
bCanBeExecuted
bIsBeingExecuted
```

这些状态已经由 GAS Tag 表达。黑板只保留聚合结果 `bIsBusy`，行为树不会因为防御系统而变得臃肿。

---

## 22. 动画资产选择与重定向

### 22.1 当前实际骨骼

主角：

```text
Mesh     /Game/SwordAnimsetPro/Demo/Character/Mesh/SK_DKMannequin
Skeleton SK_Mannequin_Skeleton
AnimBP   /Game/DKCharacter/AnimBP/Sword/ABP_DK_Sword
```

BOSS：

```text
Mesh     /Game/Dark_Knight/Dark_Knight_Male/Meshes/SKM_DKM_Full
Skeleton SK_DKM_Full
AnimBP   /Game/Enemy/AnimBP/ABP_Boss
```

`BossCharacter.h` 里旧注释提到的 `SK_DKF_Full` 已经过时，以 `BP_Boss` 当前真正使用的 `SKM_DKM_Full` 为准。

### 22.2 BOSS 普通弹反僵直

推荐源动画：

```text
/Game/Katana_Animations/Animations/Sequence/08_Hit/03_Hit_Large/AS_Hit_Large_F_Seq
```

使用：

```text
/Game/Katana_Animations/Ik/RTG_Man_DK
```

重定向到 `SKM_DKM_Full / SK_DKM_Full`，创建：

```text
AM_Boss_ParryStagger
Slot = DefaultSlot
```

代码会根据 Montage 播放长度自动计算 PlayRate，并用 `WaitDelay(1.0)` 作为真正的玩法持续时间。不要把“僵直 1”误理解为 `PlayRate = 1`；`1.0` 指秒数。

临时逻辑测试可以复用 `AM_Boss_Hit_Fwd`，但最终独立资产更容易区分普通受击与弹反反馈。

### 22.3 BOSS 可处决弱点姿势

项目当前真正存在的 In-Place 弱点资产是：

```text
/Game/SwordAnimsetPro/Animations/In-Place/Weak_01_loop_01_Anim
/Game/SwordAnimsetPro/Animations/In-Place/Weak_01_loop_02_Anim
```

`Weak_01_In_Anim` 与 `Weak_01_Recovery_Anim` 只有 Root-Motion 目录版本，没有可直接选择的同名 In-Place 版本。为了让首版和本册代码完全一致，推荐只重定向：

```text
/Game/SwordAnimsetPro/Animations/In-Place/Weak_01_loop_01_Anim
```

通过：

```text
/Game/IK/RTG_Equiped
```

重定向到 BOSS 骨骼，创建 `AM_Boss_Executable`：

```text
Section Start → Loop     （Start 与 Loop 首版都可使用 loop_01，Start 依靠短 Blend In）
Section Loop  → Loop
Slot DefaultSlot
```

当前 `GA_Boss_Executable` 超时会直接 `Montage_Stop(0.15)`，**不会**跳到 `Recover` Section，所以首版不要创建一个实际上永远不会播放的 Recover。

以后若一定要使用 Root-Motion 的 In/Recovery，需要先复制并烘焙去除根位移，再把代码改成“超时跳 `Recover`、等待 Recover 播完后结束 Ability”。这属于表现升级，不是当前首版步骤。

### 22.4 配对处决动画

推荐先只做编号 01：

```text
玩家：
/Game/Katana_Animations/Animations/Sequence/02_Attack/16_Execution/01_Execution/AS_Execution_01_Seq

BOSS：
/Game/Katana_Animations/Animations/Sequence/02_Attack/16_Execution/02_Target/AS_Execution_Target_01_Seq
```

主角动画使用：

```text
/Game/IK/RTG_Dodge
→ 重定向到 SK_DKMannequin
→ 创建 AM_DK_Execution_01
```

BOSS Target 动画使用：

```text
/Game/Katana_Animations/Ik/RTG_Man_DK
→ 重定向到 SKM_DKM_Full
→ 创建 AM_Boss_ExecutionTarget_01
```

必须使用相同编号配对：

```text
Execution_01 ↔ Execution_Target_01
```

不要把 `Execution_02` 与 `Target_01` 混用。

两个 Montage：

- 都使用 `DefaultSlot`；
- PlayRate 都保持 `1.0`；
- Blend In 尽量短且一致，例如 0.05～0.1；
- 从同一逻辑帧启动；
- 重定向后都保持 Root Motion 关闭，且 AnimBP 的 Root Motion Mode 不要只让其中一侧提取根运动；
- 不要单独裁剪一侧的首帧、尾帧或 Segment 范围，也不要只修改一侧的播放速率；
- BOSS Target Montage 关闭 `Enable Auto Blend Out`；
- BOSS Target 的受创命中帧放 `Boss.Event.Execution.ApplyDamage` Notify，并设为 `Branching Point`；
- `ApplyDamage` 必须严格早于玩家 Montage 的结束帧。

已核对的源资产 `Execution_01` 与 `Execution_Target_01` 都是 176 帧、约 `2.916667` 秒，并且都没有启用 Root Motion。重定向后在双角色测试场同帧预览：起始脚位对齐、总长一致、命中 Notify 先于玩家 `OnCompleted`。如果任一输出资产长度发生变化，先把两边统一裁剪/统一速率，再继续接代码。

首版的同步权威关系是：

```text
BOSS Target ApplyDamage Notify → 决定伤害帧；是否死亡取决于结算后的 Health
玩家 Montage OnCompleted       → 决定配对流程结束
```

因此双方长度必须保持配对；不能让玩家先播完而 BOSS 的伤害 Notify 还没到。

### 22.5 首版为什么不用 Motion Warping

当前项目没有启用 Motion Warping 插件，`First.Build.cs` 也没有模块依赖。为了一个首版处决直接引入新插件会扩大排错范围。

本册使用：

```text
BP_Boss.ExecutionPoint
→ 玩家处决开始时 TeleportPhysics 对齐
→ 临时忽略双方 Pawn 碰撞
→ 同帧启动配对动画
```

先在固定、平坦、开阔的 BOSS 战场把 `ExecutionPoint` 调准，并测试墙边、坡面和场地边缘不会把玩家传进 `WorldStatic`。当前 `sweep=false` 的固定点方案不负责自动寻找安全落点，也不支持隔墙处决；把这些限制作为首版场地设计条件。以后需要在斜坡、台阶和任意角度动态吸附时，再把这一小段替换成 Motion Warping 与落点/视线校验；Guard、Parry、Poise 与行为树结构都不需要改。

### 22.6 动画蓝图 Slot 检查

主角 `ABP_DK_Sword` 和 BOSS `ABP_Boss` 都必须在最终输出链中经过 `DefaultSlot`。如果 Montage 时间轴在运行但角色保持 locomotion：

1. 打开对应 AnimBP；
2. 检查 `DefaultSlot` 是否连接到最终姿势；
3. 检查 Montage Skeleton 是否与角色 Mesh Skeleton 一致；
4. 不要把主角重定向动画误做成 BOSS Skeleton，反之亦然。

---

## 23. 最终编辑器配置清单

### 23.1 BP_DKCharacter

```text
DKDefenseComponent
  Guard Half Angle Degrees      60
  Minimum Stamina To Guard      20
  Parry Window Duration          0.15
  Guard Move Speed               0
  Guard Break Duration           1.3

Combat | Defense | Anim
  Guard Parry Montage            AM_DK_GuardParry
  Guard Break Montage            AM_DK_GuardBreak

Combat | Execution | Anim
  Execution Montage              AM_DK_Execution_01

Combat | Execution
  Execution Damage Multiplier    10.0
```

实际处决伤害还取决于 `GE_DK_Initialize` 在运行时给出的 `AttackPower`。当前真实初始化链应从 `BP_DKCharacter → /Game/DKCharacter/Data/DA_DKStartUpData → /Game/DKCharacter/GAS/Effects/GE_DK_Initialize` 逐级检查；`/Game/DKCharacter/GAS/Data/DA_DKStartUpData` 当前只是 Redirector，不要把它误当成另一套有效配置。

例如 `AttackPower = 15` 时，本次处决造成 `150` 点原始伤害；如果测试时只有 `10` 点，说明玩家很可能仍在使用 AttributeSet 的 C++ 安全默认值 `AttackPower = 1`，应检查上述启动数据和初始化 GE。

`AM_DK_GuardParry` 与 `AM_DK_GuardBreak` 均使用 `DefaultSlot`；弹反窗口只由 Ability 的 0.15 秒 Delay 管理，不额外放 ParryWindow Notify。

### 23.2 BP_Boss

```text
Boss | Combat | Defense | Normal Attack
  Blockable                      true
  Parryable                      true
  Guard Stamina Damage           25
  Parry Poise Damage             50

Boss | Combat | Defense | Three Combo
  Blockable                      true
  Parryable                      true
  Guard Stamina Damage           15
  Parry Poise Damage             50

Boss | Combat | Poise
  Parry Stagger Duration          1.0
  Poise Recovery Delay            5.0

Boss | Combat | Execution
  Execution Request Range       260.0

Boss | Anim | Poise
  Parry Stagger Montage           AM_Boss_ParryStagger

Boss | Anim | Execution
  Executable Montage              AM_Boss_Executable
  Execution Target Montage        AM_Boss_ExecutionTarget_01
```

在组件视图中把 `ExecutionPoint` 调到 BOSS 正前方，让主角处决动画起始脚位与 BOSS Target 动画匹配。默认是本地 X +120、Yaw 180°；实际值以动画为准。

`AM_Boss_ExecutionTarget_01` 关闭 `Enable Auto Blend Out`；双方 01 号 Montage 保持相同长度、PlayRate 1、Root Motion 关闭。BOSS Target 的 `ApplyDamage` 使用 `Branching Point`，并严格放在玩家 Montage 结束之前。BOSS 存活时由 Ability 主动停止该 Montage；只有被此次处决伤害击杀时才保留最后一帧。

### 23.3 主角启动数据

当前 `BP_DKCharacter` 真正引用的 `DA_DKStartUpData`：

```text
Reactive Abilities
  UFirstGA_DKGuardBreak

Start Up Gameplay Effects
  保留 UFirstGE_StaminaRegen
  新增 UFirstGE_StaminaRegenGuarding
```

不要把下面两个运行时工具 GE 填进 Startup：

```text
UFirstGE_StaminaChange
UFirstGE_StaminaRegenPause
```

它们由防御组件按每次命中动态应用。

### 23.4 剑武器数据

`BP_DKWeapon_Sword → Default Weapon Abilities`：

```text
InputTag.GuardParry → UFirstGA_DKGuardParry
InputTag.Execute     → UFirstGA_DKExecute
```

保留现有轻攻击映射，不要覆盖数组原有元素。

沿这把武器实际引用的 `LightAttackMontages` 逐个检查：

```text
Action Cancel Window / ANS_ActionCancel
  Granted Cancel Tags
    保留 DK.Status.Action.Cancelable.By.Dodge
    新增 DK.Status.Action.Cancelable.By.Parry
```

只修改真正被武器数据引用的 Montage，不要凭同名资产猜测。

### 23.5 BOSS 启动数据

当前 `BP_Boss` 真正引用的 StartUpData：

```text
Reactive Abilities
  保留 UGA_Boss_HitReact
  保留 UGA_Boss_Death
  新增 UGA_Boss_ParryStagger
  新增 UGA_Boss_Executable
```

### 23.6 输入

```text
IMC_Default
  Right Mouse Button → IA_GuardParry
  E                  → IA_Execute

DA_InputConfig / Ability Input Actions
  IA_GuardParry → InputTag.GuardParry
  IA_Execute    → InputTag.Execute
```

### 23.7 行为树

`BT_Boss` 中现有 `Update Boss State` 节点：

```text
Action Blocking Tags
  Boss.Status.Attacking
  Boss.Status.DrawSword
  Boss.Status.Staggered
  Boss.Status.Executable
  Boss.Status.BeingExecuted
```

所有会发起攻击或移动的分支继续要求 `bIsBusy == false`，观察期中止选 `双方`。

### 23.8 初始化属性

`GE_Boss_Initialize`：

```text
Poise    100
MaxPoise 100
```

不要再配置或实现 50% 大僵直阈值。

---

## 24. 推荐实施顺序与编译闸门

不要一次性创建全部文件后才编译。按下面顺序做：

### 阶段 1：修复既有输入与 Dodge 生命周期

```text
FirstAbilitySystemComponent::OnAbilityInputReleased
FirstGA_DKDodge::EndAbility
```

验收：原闪避、攻击、装备功能没有退化。

### 阶段 2：标签与数据结构

```text
MyGameplayTags.h/.cpp
FirstCombatTypes.h
```

验收：UHT 与原生 Tag 编译通过。

### 阶段 3：所有小型 GE

```text
StaminaChange
StaminaRegenPause
StaminaRegenGuarding
修改现有 StaminaRegen
PoiseChange
ExecutionDamage
```

验收：纯编译，不接资产。

### 阶段 4：防御组件

```text
DKDefenseComponent
挂到 ADKCharacter
```

验收：`BP_DKCharacter` 能看到组件和默认参数，运行行为未改变。

### 阶段 5：主角 Guard / GuardBreak

```text
两个 Ability
Dodge 取消 GuardParry 的权限与取消列表
InputReleased 实测
输入资产
AM_DK_GuardParry
AM_DK_GuardBreak
启动数据/武器数据
```

验收：按住、Loop、松开、最低精力、空中限制和“Dodge 成功后取消 Guard”均正确；Dodge 失败时 Guard 保持。

### 阶段 6：BOSS 攻击接入防御

```text
FirstBossGameplayAbility::ResolveTargetDefense
普通攻击 / 三连 HandleMeleeHit
两个攻击 EndAbility 关碰撞
三连三个 Notify State
```

验收：正面格挡、背击、精力消耗、三段独立、破防正确。

### 阶段 7：弹反削韧

```text
BossCharacter Poise 工具与 5 秒 Timer
GA_Boss_ParryStagger
AM_Boss_ParryStagger
Reactive Ability 授予
```

验收：`100→50` 只僵直 1 秒，5 秒后回 100。

### 阶段 8：可处决

```text
GA_Boss_Executable
BT bIsBusy Tag
AM_Boss_Executable
```

验收：`50→0` 直接进入 5 秒窗口；超时回满并恢复 AI。

### 阶段 9：配对处决

```text
ExecutionPoint
GA_DKExecute
两套编号 01 配对 Montage
ApplyDamage Notify
GA_Boss_Death 特殊分支（仅处决伤害致死时使用）
```

验收：对齐、锁移动和伤害数值正确；BOSS 存活时恢复战斗，致死时保留配对死亡姿势，两条路径的清理都正确。

### 重要编译规则

每当新增以下内容时关闭编辑器完整编译，不依赖 Live Coding：

- `UCLASS`；
- `USTRUCT`；
- `UPROPERTY`；
- 原生 GameplayTag；
- 默认子对象组件。

如果编辑器仍显示旧属性布局，先关闭编辑器重新编译，再打开；不要通过重复创建蓝图变量绕过 UHT 更新。

---

## 25. 完整运行时流程

### 25.1 普通格挡

```text
右键 Started
→ ASC 激活 UFirstGA_DKGuardParry
→ 检查持剑 / 地面 / Stamina>=20 / 攻击取消窗口
→ 添加 Defending + Blocking + ParryWindow
→ 0.15 秒后移除 ParryWindow

BOSS 武器命中
→ Attack GA 传入 FFirstMeleeDefenseData
→ DKDefenseComponent 判断正面
→ Blocking 成立
→ StaminaChange(-25 或 -15)
→ StaminaRegenPause(0.75，刷新堆叠时长)
→ 发送 GuardHit 或 GuardBroken
→ 返回 Blocked
→ Attack GA 不应用 Health Damage
```

### 25.2 弹反但未破韧

```text
右键前 0.15 秒被可弹反攻击命中
→ ParryWindow 优先于 Blocking
→ 返回 Parried
→ 玩家 Guard Montage 跳到 Parry
→ 向 BOSS 发送 Boss.Event.Parried，EventMagnitude=50
→ GA_Boss_ParryStagger 激活
→ 取消 Boss.Ability.Attack
→ 攻击 EndAbility 关闭武器碰撞
→ BOSS StopMovement
→ PoiseChange(-50)
→ Poise 100→50
→ WaitDelay 1.0
→ 移除 Staggered，恢复行为树
→ 从这次削韧起 5 秒无新削韧则回满
```

### 25.3 第二次弹反破韧

```text
Poise 50 时再次弹反
→ PoiseChange(-50)
→ Poise 50→0
→ 不启动普通 1 秒 Delay
→ 发送 Boss.Event.PoiseBroken
→ GA_Boss_Executable 激活
→ 持有 Executable + Staggered
→ 停 AI / 播放 Weak Start→Loop
→ 5 秒处决窗口
```

### 25.4 超时未处决

```text
BossCharacter 的五秒 Timer 到期
→ 发送 Boss.Event.ExecutableExpired
→ GA_Boss_Executable 结束
→ Executable / Staggered 自动移除
→ RestorePoiseToFull
→ UI 更新到 100
→ bIsBusy=false
→ 行为树重新选择攻击/后退/周旋/靠近
```

### 25.5 成功结算处决

```text
玩家距离 <=200，按 E
→ GA_DKExecute 找最近 Executable BOSS
→ 验证双方 Montage
→ 禁用玩家移动、临时忽略双方 Pawn 碰撞
→ 玩家对齐 Boss.ExecutionPoint
→ 向 BOSS 发送 ExecutionStarted
→ BOSS 清除五秒 Timer，添加 BeingExecuted
→ 同帧播放 Execution_01 与 Target_01
→ Target_01 命中帧发送 Execution.ApplyDamage
→ DamageTaken = max(玩家当前 AttackPower, 0) × 10
→ 血条按实际伤害更新
→ 玩家 Montage 完成发送 ExecutionFinished
├─ BOSS 存活
│  ├─ 移除临时 Executed，停止 Target Montage
│  ├─ Poise 回满，玩家/BOSS 的 Pawn 碰撞恢复
│  └─ Executable / Staggered / BeingExecuted 清理，行为树恢复
└─ BOSS 生命归零
   ├─ 添加 Shared.Status.Dead
   ├─ Death Ability 保留处决 Target Montage
   ├─ 清理 BeingExecuted；死亡 BOSS 对玩家继续保持 Pawn Ignore
   └─ BOSS 保持 Target Montage 最后一帧
```

---

## 26. 最低测试矩阵

### 26.1 格挡方向与精力

| 场景 | 预期结果 |
|---|---|
| 未按右键，正面普通攻击 | 正常扣血 |
| 正面格挡普通攻击 | 不扣血，精力 -25 |
| 正面格挡三连三段 | 每段分别 -15 |
| 侧面攻击 | 正常扣血，不扣格挡精力 |
| 背后攻击 | 正常扣血，不扣格挡精力 |
| 攻击者在正面 60° 边界内 | 可以格挡 |
| 攻击者超过单侧 60° | 不能格挡 |
| 格挡命中后 0.75 秒 | 普通和慢恢复都暂停 |
| 持续格挡且未受击 | 只按 2/秒恢复 |
| 松开格挡 | Blocking 当帧消失，End 播完后 Defending 消失 |
| 精力低于 20 再按右键 | Guard 激活失败 |
| 空中按右键 | Guard 激活失败 |
| 未装备剑按右键 | Guard 激活失败 |
| Guard 中按空格 | 不起跳，Blocking/ParryWindow 不会被带到空中 |
| Guard 中按闪避且 Dodge 可用 | 先成功 Commit，再取消 GuardParry；Blocking/ParryWindow 被 Guard 自己清理并进入闪避 |
| Guard 的 Start/Loop/Hit/Parry/End 任一阶段按可用闪避 | 当前 Guard Montage 与标签立即清理，只留下 Dodge 生命周期 |
| Guard 中按闪避但精力不足或仍在冷却 | Dodge 不启动，Guard 保持，不丢失 Blocking |
| 按住右键转入闪避后仍不松右键 | 闪避结束不会自动重新格挡；必须松开并再次按下右键 |
| 奔跑中举盾后松开 Shift | Guard 期间仍为 GuardMoveSpeed，退出后恢复 WalkSpeed |
| Guard 中一直按住 Shift | Guard 期间不加速，退出后按当前 bIsRunning 恢复 RunSpeed |

### 26.2 破防与三连后续段

| 场景 | 预期结果 |
|---|---|
| 某一段把精力扣到 0 | 这一段仍不扣血，立即进入破防 |
| 破防后同一三连的下一段命中 | 正常扣血 |
| 破防 1.3 秒内按攻击/闪避/格挡 | 都不能启动 |
| 破防结束但精力仍低于 20 | 仍不能格挡 |
| 精力自然恢复到 20 | 可以再次格挡 |
| 破防 1.3 秒内 | 普通 20/秒与格挡 2/秒恢复都不运行 |

### 26.3 弹反与韧性

| 场景 | 预期结果 |
|---|---|
| 0.15 秒内弹反普通攻击 | 不扣血、不扣精力；BOSS 攻击取消，Poise -50 |
| 0.15 秒后仍按住 | 退化为普通格挡，不触发弹反 |
| 背后处于 ParryWindow | 不能弹反，正常受伤 |
| 弹反三连第一段 | 整套三连取消，后两段不再命中 |
| 弹反三连第二/第三段 | 当段成功后整套剩余动画取消 |
| 第一次弹反 100→50 | 只僵直 1.0 秒，不触发旧大僵直 |
| 第一次弹反后等待超过 5 秒 | Poise 回到 100 |
| 第一次弹反后 5 秒内第二次成功 | 50→0，直接 Executable |
| 第二次弹反到 0 | 不再额外播放普通 1 秒弹反僵直 |
| 未来投技命中 Blocking | 不格挡、不扣格挡精力 |
| 未来投技命中 ParryWindow | 不弹反，投技正常继续 |

### 26.4 行为树与碰撞

| 场景 | 预期结果 |
|---|---|
| BOSS 在 MoveTo 中被弹反 | 当帧停止，不滑行 |
| 弹反后 1 秒内 | 行为树不进入攻击、后退、周旋、靠近 |
| 普通弹反结束 | bIsBusy 变 false，行为树恢复 |
| BOSS 武器有效帧中被弹反 | 碰撞立即关闭，不再造成幽灵伤害 |
| 三连被弹反后再次攻击 | OverlappedActors 已清空，可正常命中 |
| Executable 期间 | 行为树一直被挡住 |
| Executable 超时 | AI 恢复，不永久卡死 |
| Executable 弱点待机中被外部取消 | 只要 BOSS 仍活着，Poise 回满、`bIsBusy=false`，不会以 0 韧性恢复战斗 |

### 26.5 处决

| 场景 | 预期结果 |
|---|---|
| Poise > 0 按 E | 不启动处决 |
| Executable 但距离 >200 | 不启动处决 |
| 200 内按 E | 玩家对齐 ExecutionPoint，双方同帧播放 |
| BOSS 漏授予 Executable Ability / Target Montage 无法播放 | BOSS 不确认请求；玩家不传送、不锁移动、不独自播放 |
| 处决期间移动/攻击/闪避输入 | 不能改变角色位置或启动其它动作 |
| 处决期间受到普通伤害 | 主角有 Invincible，不扣血 |
| 伤害帧前 BOSS Executable Ability 被外部强制取消 | BOSS 发送 `DK.Event.ExecutionAbortedByBoss`，玩家立即退出锁移动/无敌 |
| 玩家 AttackPower=15，BOSS 当前 Health=500 | `ApplyDamage` 后准确扣 150，Health=350，不出现 `Shared.Dead` |
| 玩家 AttackPower=15，BOSS 当前 Health≤150 | `ApplyDamage` 后 Health=0，出现 `Shared.Dead` |
| 非致死处决的血条 | 命中帧准确减少 150，而不是等动画结束才刷新 |
| 只把 BOSS DefensePower 改成其它值 | 处决仍准确造成 150，证明没有误走普通攻防公式 |
| 同一段 Montage 的 ApplyDamage 意外重复触发 | `bExecutionDamageApplied` 拦截，整次处决只扣一次伤害 |
| 伤害结算后玩家动画被中断 | 不返回 Executable 重复处决；BOSS 存活时完成清理并恢复战斗 |
| 非致死伤害结算后，BOSS 被其它伤害击杀或 Executable 被外部取消 | BOSS 立即发送玩家中止事件；玩家不再独自锁移动/无敌到动画自然结束 |
| 非致死处决完成 | Target Montage 停止、Poise 回满、`bIsBusy=false`，双方 Pawn 碰撞恢复 |
| 非致死后再被普通攻击击杀 | 因临时 `Boss.Status.Executed` 已移除，正常播放通用 DeathMontage |
| 致死处决完成 | 普通 DeathMontage 不覆盖 Target；死亡 BOSS 对 Pawn 保持 Ignore，并保留最终姿势 |
| 漏配 ApplyDamage Notify | 输出 Error，BOSS 返回 Executable，可重试 |
| Executable 超过 5 秒未按 E | 退出状态、Poise 回满、恢复战斗 |
| 墙后、楼层上下或 ExecutionPoint 穿墙 | 首版明确不支持；测试场必须使用无遮挡平地并调整锚点 |

### 26.6 旧功能回归

| 场景 | 预期结果 |
|---|---|
| 玩家普通闪避 | 仍只结束一次 Ability，无敌窗正常 |
| 玩家攻击的 `.By.Dodge` 窗口内按闪避 | 原攻击仍能被取消并进入 Dodge |
| 玩家攻击的 `.By.Dodge` 窗口外按闪避 | Dodge 仍不能取消该攻击 |
| 玩家轻攻击连招 | 原连招与伤害正常 |
| BOSS 普通攻击冷却 | 仍为 4 秒 |
| BOSS 三连冷却 | 仍为 8 秒 |
| BOSS 普通受击窗口内受击 | 保持当前既有行为 |
| BOSS 普通受击窗口外受击 | 仍不播放普通受击；本册没有改这项 |
| 普通伤害把 BOSS 打死 | 仍播放原通用 DeathMontage |
| 主角锁敌 | 不参与正面格挡判定，死亡 BOSS 按原逻辑断锁 |

---

## 27. 常见问题与定位顺序

### 27.1 格挡动画播放了，但主角仍然掉血

按顺序检查：

1. `HandleMeleeHit` 是否在创建 `UFirstGE_Damage` **之前**调用 `ResolveTargetDefense`；
2. 是否只有 `DefenseResult == Damaged` 才应用伤害；
3. `BP_Boss` 对应攻击的 `bBlockable` 是否为 true；
4. 主角 ASC 是否真的有 `DK.Status.Blocking`；
5. 攻击者是否在主角 Actor Forward 的总 120° 内；
6. 是否错误地仍保留了旧版本的 HandleMeleeHit 伤害代码，导致同一事件应用两次伤害。

### 27.2 每次及时按右键都只是普通格挡，不触发弹反

检查：

- `bParryable` 是否为 true；
- `ParryWindow` 是否在按下第一帧加入；
- 防御组件是否先判断 ParryWindow，再判断 Blocking；
- `Boss.Event.Parried` Tag 字符串是否完全一致；
- `UGA_Boss_ParryStagger` 是否已加入 BOSS ReactiveAbilities；
- 右键能力是否因为输入映射重复而先启动又立刻松开。

### 27.3 松开右键后仍一直格挡

先检查第 6.1 节：

```text
AbilitySpec.InputPressed = false
InvokeReplicatedEvent(InputReleased, ...)
```

再检查：

- `IA_GuardParry` 是否 Boolean；
- `BindAbilityInputActions` 是否同时绑定 Completed/Canceled 到 Released；
- `WaitInputRelease(this, true)` 是否创建并 `ReadyForActivation`；
- `BeginGuardExit` 是否立即移除 Blocking；
- `End` Section 是否链接到“无”。

### 27.4 按住右键只能维持一小会儿

`AM_DK_GuardParry` 的 `Loop` 没有指向自己，Montage 自然完成，`OnCompleted` 结束了 Ability。

### 27.5 三连只扣一次精力

通常不是防御组件问题，而是三连 Montage 只有一个长武器碰撞窗口。

检查三个独立 Notify State 的 End 是否真实执行：

```text
每一段 End
→ ToggleWeaponCollision(false)
→ OverlappedActors.Empty()
```

### 27.6 弹反后动画停了，但剑仍能伤人

两个攻击类都必须覆写 `EndAbility` 并在唯一一次 `Super::EndAbility` 之前：

```cpp
Combat->ToggleWeaponCollision(false);
```

同时 `GA_Boss_ParryStagger` 激活时再调用一次作为双保险。

### 27.7 BOSS 弹反后还在滑动或立刻重新 MoveTo

要同时满足：

```text
ParryStagger 激活时立即 StopMovement
BT 服务 ActionBlockingTags 包含 Boss.Status.Staggered
移动分支要求 bIsBusy == false
装饰器 Observer Aborts = Both
```

尤其要检查行为树中**已放置的服务节点详情**。只改 C++ 构造默认值不一定更新旧节点保存的数组。

### 27.8 第一次弹反就进入可处决或大僵直

检查：

- `GE_Boss_Initialize` 是否确实应用 `100/100`；
- `BP_Boss` 的 ParryPoiseDamage 是否误填 100；
- 是否还保留旧的 `Poise <= MaxPoise * 0.5` 大僵直判断；
- 是否有普通玩家攻击正在额外削韧；本册首版不应有。

### 27.9 第二次弹反 Poise=0，但 BOSS 继续战斗

检查：

- `Boss.Event.PoiseBroken` 是否发送；
- `UGA_Boss_Executable` 是否加入 ReactiveAbilities；
- 它是否被错误的 ActivationBlockedTags 阻止；
- `BT_Boss` 的 bIsBusy 是否读取 `Boss.Status.Staggered/Executable`。

### 27.10 格挡期间精力恢复仍然很快

说明普通 20/秒和慢恢复 2/秒同时在执行。检查普通 `UFirstGE_StaminaRegen` 是否通过 UE5.6 的：

```cpp
UTargetTagRequirementsGameplayEffectComponent
```

忽略了 `DK.Status.Blocking`。不要使用已弃用字段，也不要只新增慢恢复而忘记抑制普通恢复。

### 27.11 连续挡三段后，0.75 秒暂停叠成了 2.25 秒

`UFirstGE_StaminaRegenPause` 的堆叠配置不正确。确认：

```text
StackingType = AggregateByTarget
StackLimitCount = 1
StackDurationRefreshPolicy = RefreshOnSuccessfulApplication
```

正确结果是每段刷新“从最新一段起 0.75 秒”，不是把三次时长相加。

### 27.12 处决双方位置错开

按顺序处理：

1. 确认使用同编号 `Execution_01 / Target_01`；
2. 确认 PlayRate 相同；
3. 确认双方 Montage Blend In 接近；
4. 在 `BP_Boss` 调整 `ExecutionPoint` 的位置与 Yaw；
5. 检查重定向后的根骨骼朝向；
6. 首版不要再额外启用 Motion Warping 或 Root Motion 位移补偿，否则会产生双重对齐。

### 27.13 处决动画播完没有扣血，或 BOSS 没死

先区分两种情况：**血量按 `AttackPower × 10` 正常下降但仍大于 0，是正确设计，不是故障。** 只有血量完全没变，或扣除值不等于公式时才继续排查。

查看日志是否出现：

```text
Boss execution finished without Boss.Event.Execution.ApplyDamage
```

出现则说明 BOSS Target Montage 没有放 `DK Gameplay Event` Notify，或 Event Tag 不是：

```text
Boss.Event.Execution.ApplyDamage
```

Notify 已触发但数值不对时，再检查：

- `BP_DKCharacter → Combat | Execution → Execution Damage Multiplier` 是否为 `10`；
- `GE_DK_Initialize` 实际给玩家的 `AttackPower`；
- 是否使用 `UFirstGE_ExecutionDamage` 直接写 `DamageTaken`；
- 是否错误走了普通 `UFirstGE_Damage`，导致再次乘 AttackPower、再除 DefensePower。

### 27.14 处决伤害致死时突然切到普通死亡动画

检查：

- `Boss.Status.Executed` 是否在应用 `ExecutionDamage` **之前**临时加入；
- `GA_Boss_Death` 是否在该 Tag 存在时跳过 `CancelAllAbilities`；
- 是否在该分支跳过通用 `DeathMontage`。

### 27.15 致死后尸体站起，或非致死后 BOSS 一直定格

致死后尸体站起时，检查 BOSS Target Montage：

```text
Enable Auto Blend Out = false
```

并确认 `bBossKilledByExecution == true` 时，`GA_Boss_Executable::EndAbility` 没有主动停止 Target Montage。

非致死后仍定格或 AI 不恢复时，按顺序检查：

1. `HandleExecutionApplyDamage` 是否把 `bExecutionDamageApplied` 设为 true，并移除了临时 `Boss.Status.Executed`；
2. `HandleExecutionFinished` 是否正常结束 Executable Ability，而不是把“没死”误判成失败；
3. `EndAbility` 是否显式停止 Target Montage、调用 `RestorePoiseToFull()`；
4. `Executable / Staggered / BeingExecuted` 是否都已移除，`bIsBusy` 是否回到 false；
5. 玩家 `GA_DKExecute::EndAbility` 是否在 BOSS 存活时恢复了双方原来的 Pawn 碰撞响应。

### 27.16 退出格挡后移动速度不对

不要恢复进入 Guard 时保存的旧 `MaxWalkSpeed` 快照。Guard 期间 Shift 可能已经按下或松开，结束时应通过：

```cpp
DK->GetDesiredLocomotionSpeed()
```

根据当前 `bIsRunning` 重新得到 250/500。再检查 `SetRunning` 是否在 `Defending / GuardBroken / Executing` 期间只更新 `bIsRunning`、不覆盖动作速度。交叉测试：奔跑中举盾后松 Shift、举盾中按住 Shift、破防中松 Shift，动作结束后的速度都应符合当前按键状态。

### 27.17 新增 C++ 类编译通过但编辑器里找不到

关闭编辑器，完整编译 Editor Target，再启动。新增 `UCLASS/USTRUCT/UPROPERTY` 时不要依赖 Live Coding。若仍不显示，检查文件是否位于 `Source/First` 模块并含正确的 `FIRST_API` 与 `generated.h`。

### 27.18 按住格挡时无法闪避，或闪避失败却退出了格挡

如果完全无法闪避，依次检查：

1. `UFirstGA_DKDodge` 构造函数是否仍把 `DK.Status.Defending` 加进了 `ActivationBlockedTags`；
2. `UFirstGA_DKGuardParry` 是否持有 `DK.Status.Action.Cancelable.By.Dodge`；
3. Dodge 的 `CanActivateAbility()` 是否把 `Attacking / Defending` 都交给 `.By.Dodge` 权限判断；
4. `CancelDodgeInterruptibleAbilities()` 是否同时包含 `DK.Ability.Attack` 和 `DK.Ability.GuardParry`；
5. `GA_DKGuardParry` 的 `AssetTags` 是否确实包含 `DK.Ability.GuardParry`。

如果闪避因为精力不足或冷却失败，却仍然退出了格挡，说明取消调用放在了错误的位置。顺序必须是：

```text
CommitAbility 成功
→ CancelDodgeInterruptibleAbilities
```

不能在 `CommitAbility()` 之前取消 Guard。也不要由 Dodge 手动移除 `Blocking / ParryWindow`；取消 Guard Ability 后，它自己的 `EndAbility()` 会完成清理。

---

## 28. 本册暂不实现的内容

以下内容明确留到后续，不要在首轮一起加入：

- 普通玩家轻/重攻击对 BOSS 的韧性伤害；数值尚未决定；
- 50% 韧性大僵直；已经取消，不是待办；
- 多种弹反方向与左右随机动画；首版先用一个 Parry Section；
- 格挡移动 Blend Space；当前速度 0，避免滑步；
- 精力条闪烁、破防特效、弹反火花、音效和镜头震动；
- 处决提示 UI；首版直接用距离 + E；
- 多套处决随机选择；先完成编号 01；
- Motion Warping；首版固定 ExecutionPoint；
- 网络复制与预测；
- 投技本体实现；本册只固定它的防御资格为 false/false；
- 当前“非攻击状态 BOSS 不播放普通受击”的问题；继续保持不变。

以后增加普通武器削韧时，可以复用 `UFirstGE_PoiseChange` 和 `ABossCharacter::ApplyPoiseDelta`，但要注意：**当前 `ApplyPoiseDelta` 只负责改数值和重启五秒计时，本身不会自动发送 `Boss.Event.PoiseBroken`。**

每个新的削韧来源在应用后都必须检查最终 Poise；首次到 0 时发送一次 `Boss.Event.PoiseBroken`。更推荐后续把“应用 Delta + 检查 0 + 防重复发事件”集中封装到 `ABossCharacter` 的一个新接口，避免各攻击遗漏或重复触发。`GA_Boss_Executable` 已兜底取消 `Boss.Ability.Attack` 并关闭武器碰撞，所以未来即使普通攻击过程中被削到 0，也不会继续挥砍。

无论采用哪种封装，都不要重新引入 50% 阈值：

```text
Poise > 0 → 按来源自己的僵直规则
Poise == 0 → Boss.Event.PoiseBroken → Executable
```

---

## 29. 最终职责总结

| 系统 | 只负责什么 | 不负责什么 |
|---|---|---|
| BOSS 攻击 GA | 描述招式防御资格；命中前询问组件；Damaged 时扣血 | 不计算正面角度，不管理玩家格挡动画 |
| `UDKDefenseComponent` | 一次命中的正面/格挡/弹反结果与精力结算 | 不认识具体招式类名，不播放 BOSS 动画 |
| `GA_DKGuardParry` | 按住、松开、ParryWindow、Blocking、格挡 Section，并授予 `.By.Dodge` 取消权限 | 不扣 BOSS 韧性，不自行启动 Dodge |
| `GA_DKDodge` | 成功 Commit 后取消带权限的 Attack/Guard，并管理闪避表现 | 不在启动失败时取消 Guard，不手动清理 Guard 的 Loose Tag |
| `GA_DKGuardBreak` | 破防事件、1.3 秒锁移动和破防 Montage | 不直接赠送精力 |
| 精力 GE | 普通恢复、慢恢复、命中暂停、即时扣除 | 不判断攻击方向 |
| `GA_Boss_ParryStagger` | 取消攻击、关碰撞、削 50 韧性、1 秒普通僵直 | 不复用普通 HitReactWindow |
| `ABossCharacter` | Poise GE 入口、五秒计时、处决点和动画数据 | 不决定玩家是否成功格挡 |
| `GA_Boss_Executable` | 可处决生命周期、BOSS 配对动画、精确伤害帧及存活/死亡分流 | 不处理玩家输入 |
| `GA_DKExecute` | 搜索最近目标、对齐、玩家配对动画和清理 | 不计算或应用处决伤害 |
| `GA_Boss_Death` | 区分普通死亡与“处决伤害致死” | 不重新播放处决动画 |
| 行为树 | 通过 `bIsBusy` 停止/恢复宏观决策 | 不保存 Parry、Poise、Execution 的重复状态 |

最终核心边界只有一句：

```text
攻击 Ability 描述规则，DefenseComponent 判定结果，GameplayAbility 管理动作生命周期，GameplayEffect 修改数值，BehaviorTree 只决定下一步做什么。
```

按第 24 节逐阶段实施和编译，就能在不改动现有普通受击窗口逻辑的前提下，得到可扩展的格挡、弹反、破防、韧性归零与处决闭环。
