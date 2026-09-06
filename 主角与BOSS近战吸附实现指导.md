# 主角与 BOSS 近战吸附实现指导（Motion Warping）

> 适用项目：`First`（UE 5.6 / C++ / GAS / 中文版编辑器）
>
> 插件：Motion Warping（引擎自带，**当前项目尚未启用**，本册第一步启用）
>
> 前置条件：主角攻击（四段连击）、BOSS 普通攻击/三连、武器碰撞窗口均可用；主角/BOSS 的攻击蒙太奇使用**带根运动的攻击动画**（已确认）。
>
> 本文只提供实现指导，不会自动修改 C++、蓝图或蒙太奇。

---

## 0. 最终效果

```text
主角/BOSS 发起攻击
→ 攻击起手后（命中前窗口内）身体与脚步自动向目标前进并
   将挥砍方向修正到目标方向（胸口位置）
→ 目标侧移时攻击仍然“追上”目标，不落空、不隔空吸附
→ 目标距离超过吸附范围时，只做朝向修正，不做位置吸附
→ 连击每一段都重复吸附（每段各自有窗口）
→ 攻击被取消/打断/命中后窗口结束，吸附立即停止
```

## 1. 本册设计

### 1.1 为什么用 Motion Warping（而不是自写位移）

攻击动画本身带有**根部位移**（向前跨步/突进）。Motion Warping 的作用是：在蒙太奇的一个**窗口**里，把这段已有根位移按“当前目标的位置/朝向”重新对齐：

```text
无吸附：动画根位移 → 固定朝角色攻击起手时的朝向走（目标侧移就落空）
有吸附：动画根位移 → 被“旋转+平移”对齐到 目标胸口 → 自动修正方向与距离
```

- **方向修正**：窗口内角色朝向逐步转到目标方向；
- **距离修正**：根位移的终点被对齐到目标（胸骨挂点），吸过头与否由动画自身的位移量决定。

### 1.2 三个关键参数定义

| 项 | 说明 | 本册建议 |
|---|---|---|
| **Warp 窗口** | 蒙太奇时间轴上“Motion Warping 通知状态”的起止 | 每段：起手帧 → 命中（武器碰撞窗口）前 0.03 秒 |
| **WarpTarget** | 吸附的目标点（目标 Actor + 骨骼挂点） | 目标 Actor 的**胸口骨骼**（如 `spine_03`，以实际骨架为准） |
| **吸附量** | 不是独立参数！**由动画自身的根位移量决定** | 想吸更近/更远：调整蒙太奇根运动段时长或 `RootMotionTranslationScale` |

### 1.3 目标来源

```text
主角：攻击启动时取 DKTargetLockComponent 当前锁定目标；
      未锁定时取攻击朝向前方 250 内最近的 Pawn（首版可只做锁定目标，见 §6）
BOSS：攻击启动时取黑版 TargetActor
```

### 1.4 本册不变的内容

- 武器碰撞/Hit 事件链不变（吸附只改变“碰撞窗口发生的位置”）；
- 格挡/弹反/破韧/处决等判定不变；
- 取消框架、连击窗口、精力消耗等不变。

## 2. 已确认的事实（与需在编辑器里二次确认的）

| 项 | 状态 |
|---|---|
| 主角攻击蒙太奇 | `/Game/DKCharacter/AnimBP/Sword/Montages/Attack/AM_DK_Sword_Attack_1~4`（武器数据引用） |
| 主角攻击动画 | `A_DK_Sword_Attack_1~4`（**带根运动**，已确认） |
| BOSS 攻击蒙太奇 | `/Game/Enemy/AnimBP/Montages/Attacks/AM_Boss_Attack_Normal`、`AM_Boss_Attack_Combo` |
| BOSS 攻击动画 | `Anim_DKM_Attack_01~03`（带根运动，已确认） |
| Mock Warping 插件 | ❌ 未启用（本册第一步） |
| 主角锁定目标 | `DKTargetLockComponent`（IsTargetLocked / GetTarget... 以实际 API 为准） |
| BOSS 目标 | 黑版 `TargetActor` |
| 胸口骨骼名 | ⚠️ 需确认：打开主角骨架 & DKM 骨架，搜索 `spine`，记录实际骨骼名（本合同以 `spine_03` 举例） |

## 3. 改动总览

| 文件/资产 | 操作 | 内容 |
|---|---|---|
| `First.uproject` | 修改 | 启用 `MotionWarping` 插件 |
| `First.Build.cs` | 修改 | 增加 `MotionWarping` 模块依赖 |
| `BaseCharacter.h/.cpp` | 修改 | 挂载 `UMotionWarpingComponent`（主角/BOSS 共用） |
| 玩家攻击 GA | 修改 | 攻击启动时设置 WarpTarget（锁定目标/胸口骨骼） |
| BOSS 攻击 GA | 修改 | 攻击启动时设置 WarpTarget（TargetActor/胸口骨骼） |
| 6 个攻击蒙太奇 | 配置 | 每段放置 1 个 `Motion Warping` 通知状态 |

## 4. 第一步：启用 MotionWarping 插件

### 4.1 `First.uproject`

在 `Plugins` 数组末尾增加：

```json
		{
			"Name": "MotionWarping",
			"Enabled": true
		}
```

### 4.2 `First.Build.cs`

`PublicDependencyModuleNames` 中增加：

```cpp
		"MotionWarping",
```

### 编译闸门 1

关闭编辑器 → 重新打开项目（插件启用后需要重启编辑器）→ 确认编辑器不再提示“缺少模块/插件错误”。此时还没有任何 C++ 引用，行为应与之前一致。

## 5. 第二步：给角色挂载 MotionWarpingComponent

主角（`ADKCharacter`）与 BOSS（`ABossCharacter`）都需要。推荐挂在共用的 `ABaseCharacter` 上，一处生效。

### 5.1 修改 `BaseCharacter.h`

文件：

```text
Source/First/Public/Character/BaseCharacter.h
```

前置声明区域增加：

```cpp
class UMotionWarpingComponent;
```

组件属性区域（其它组件附近）增加：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;
```

public 区域增加 accessor：

```cpp
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const
	{
		return MotionWarpingComponent;
	}
```

### 5.2 修改 `BaseCharacter.cpp`

顶部 include：

```cpp
#include "MotionWarpingComponent.h"
```

构造函数中创建：

```cpp
	MotionWarpingComponent =
		CreateDefaultSubobject<UMotionWarpingComponent>(
			TEXT("MotionWarpingComponent"));
```

### 编译闸门 2

完整编译后，`BP_DKCharacter` 与 `BP_Boss` 的组件面板中都能看到 `MotionWarpingComponent`（默认配置即可）。

## 6. 第三步：攻击启动时设置 WarpTarget

Warp Target 的名称与蒙太奇里“Motion Warping 通知状态”里的 `Warp Target Name` 必须一致。以 `AttackTarget` 为例：

### 6.1 主角：`FirstGA_DKLightAttack.cpp`

在实际工程中，这段代码放在 `StartCurrentComboStep()`（每一段连击开始都会进入），关键 API 使用 **UE 5.6 的 `AddOrUpdateWarpTargetFromComponent`**：

```cpp
	ADKCharacter* DK = GetDKCharacterFromActorInfo();
	UDKTargetLockComponent* TargetLock =
		DK ? DK->GetTargetLockComponent() : nullptr;

	// 目标 = 当前锁定目标；未锁定时保持既有行为（不吸附）。
	if (DK && DK->GetMotionWarpingComponent() &&
		TargetLock && TargetLock->IsTargetLocked())
	{
		if (AEnemyCharacter* WarpTarget = TargetLock->GetCurrentTarget())
		{
			// 距离闸门：超过 MaxWarpRange 不吸附。引擎在窗口内无根位移时会“补位移直达目标”，
			// 没有距离上限——必须在这里截断，否则会出现跨越大距离瞬移（五六个身位）。
			const float MaxWarpRange = 300.f;
			if (FVector::DistSquared2D(
					DK->GetActorLocation(),
					WarpTarget->GetActorLocation()) <=
				FMath::Square(MaxWarpRange))
			{
				// 止步距离（参考 slash3）：吸附终点停在目标前方 WarpStopDistance，避免贴脸/穿模。
				const float WarpStopDistance = 90.f;
				FVector DirTargetToAttacker =
					DK->GetActorLocation() - WarpTarget->GetActorLocation();
				DirTargetToAttacker.Z = 0.f;
				const FVector WarpStopOffset =
					DirTargetToAttacker.GetSafeNormal() * WarpStopDistance;
				const FVector WarpOffset =
					WarpStopOffset + FVector(0.f, 0.f, 25.f);

				// 组件必须用【根胶囊】而不是 Mesh：网格常带导入旋转偏移，用 Mesh 会让攻击者横置。
				DK->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromComponent(
					FName(TEXT("AttackTarget")),
					WarpTarget->GetCapsuleComponent(),
					FName(NAME_None),
					true,
					EWarpTargetLocationOffsetDirection::WorldSpace,
					WarpOffset,
					FRotator::ZeroRotator);
			}
		}
	}
```

> 引用 TargetLock 需在文件顶部 include `Components/Targeting/DKTargetLockComponent.h`（主角攻击 GA 通常已包含，没有就补）。

### 6.2 BOSS：`GA_Boss_NormalAttack.cpp` / `GA_Boss_ThreeCombo.cpp`

在各自 `ActivateAbility()` 的 `CommitAbility` 成功之后：

```cpp
	ABossCharacter* Boss = GetBossCharacterFromActorInfo();
	if (Boss && Boss->GetMotionWarpingComponent())
	{
		// 目标来自黑版 TargetActor
		if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
		{
			if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			{
				if (AActor* TargetActor = Cast<AActor>(
					BB->GetValueAsObject(TEXT("TargetActor"))))
				{
					if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
					{
						// 距离闸门：超过 MaxWarpRange 不吸附。引擎在窗口内无根位移时会“补位移直达目标”，
						// 没有距离上限——必须在这里截断，否则会出现跨越大距离瞬移（五六个身位）。
						const float MaxWarpRange = 300.f;
						if (FVector::DistSquared2D(
								Boss->GetActorLocation(),
								TargetCharacter->GetActorLocation()) <=
							FMath::Square(MaxWarpRange))
						{
							// 止步距离（参考 slash3）：吸附终点停在目标前方 WarpStopDistance，避免贴脸/穿模。
							const float WarpStopDistance = 90.f;
							FVector DirTargetToAttacker =
								Boss->GetActorLocation() - TargetCharacter->GetActorLocation();
							DirTargetToAttacker.Z = 0.f;
							const FVector WarpStopOffset =
								DirTargetToAttacker.GetSafeNormal() * WarpStopDistance;
							const FVector WarpOffset =
								WarpStopOffset + FVector(0.f, 0.f, 25.f);

							// 组件必须用【根胶囊】而不是 Mesh：网格常带导入旋转偏移，用 Mesh 会让攻击者横置。
							Boss->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromComponent(
								FName(TEXT("AttackTarget")),
								TargetCharacter->GetCapsuleComponent(),
								FName(NAME_None),
								true,
								EWarpTargetLocationOffsetDirection::WorldSpace,
								WarpOffset,
								FRotator::ZeroRotator);
						}
					}
				}
			}
		}
	}
```

要点：

- 目标 Actor 在攻击中不会变，每段连击重复调用是安全的（覆盖同一 WarpTarget）；
- 不需要“清理”代码——下次攻击开始会覆盖；蒙太奇结束时 Warp 窗口自动失效；
- **方案 A（定稿）——朝向由游戏侧权威，Warp 只做位移**：
  - 三个攻击 GA（主角轻攻击 / BOSS 普攻 / BOSS 三连）在攻击期间用 0.02s 定时器
    `HandleAttackFaceTick()` 每帧把攻击者朝向设为面向目标当前位置（EndAbility 停止）；
  - 通知状态 `bWarpRotation = false`（不依赖 Warp 的旋转行为）；
  - 这样动画根运动的旋转即使存在也会被逐帧覆盖，玩家/BOSS 移动、倒退、侧移均不可能背身或侧转；
- **WarpTarget 用根胶囊（GetCapsuleComponent）而不是 Mesh**：网格组件常带导入旋转偏移（-90°），用 Mesh 会把该偏移对齐到攻击者身上，导致攻击者横置 90°（实测踩坑）；
- `MaxWarpRange = 300`：距离闸门（修复"五六个身位瞬移"的关键——引擎在窗口内无根位移时会补位移直达目标，必须在这里截断）；
- `WarpStopDistance = 90`：止步距离（参考 slash3），吸附终点 = 目标前方 90cm；
- `EWarpTargetLocationOffsetDirection::WorldSpace` + 偏移矢量：水平 = 止步距离方向，垂直 = 胸口高度抬升（25 按身高微调）。

### 编译闸门 3

完整编译后，进 PIE 用“GAS Debugger/日志”确认：攻击启动时没有报错，且 WarpTarget 已设置（组件可在 BP 细节/动画查看器中看到 Warp Target）。

## 7. 第四步：在攻击蒙太奇上放置 Motion Warping 通知状态

### 7.1 放置方法（每个攻击蒙太奇）

1. 打开攻击蒙太奇（主角 4 段 + BOSS 普攻 + BOSS 三连）；
2. 在时间轴上，**从“起手帧”拖到“命中帧前 0.03 秒”** 的区间 → 右键 → **Add Notify...** → 搜索 **`Motion Warping`**（插件自带通知状态，不是我们自己写的单帧通知）→ 放置；
3. 选中该通知状态，右侧细节：

```text
Warp Target Name = AttackTarget        ← 与 §6.3 的 C++ 名称一致
Warp Target 类型  = Component（使用组件上已设置的 Warp Target）
bWarpTranslation = true                 ← 只保留位移吸附
bWarpRotation    = false                ← 关闭旋转 Warp！
                                          （实测：默认的 Default 会背身 180°、
                                           Facing 在目标移动时仍会结算成背向——
                                           旋转必须交给角色自己控制，见 §6.2 的面向修正）
Rotation Method  = Slerp（保持）
```

> 中文编辑器里的字段名可能与上面不一致，按“Warp Target Name”这个字段为准对应。

### 7.2 每段窗口的推荐对齐

```text
起手帧                     命中窗（FirstANS_WeaponCollision）
  │            Motion Warping 窗口          │
  └────────────┬─────────────────────────────┘
```

- 窗口起点：攻击出招后的 0.05 秒（避开完美站立帧）；
- 窗口终点：武器碰撞窗口（`Weapon Collision` 通知状态）**起点前 0.03 秒**——命中后立即停止吸附，防止穿模/顶着目标推；
- 主角 4 段 + BOSS 普攻 + 三连的**每段**都按此规则放一个窗口（三连是三段动画的单个蒙太奇：每段各放一个窗口）。

### 7.3 蒙太奇根运动检查

Motion Warping 依赖窗口内的根位移。确认：

1. 打开攻击序列/蒙太奇 → **Details → Root Motion**：`Enable Root Motion` 处于启用状态（你已确认带根运动，此项通常已开）；
2. 播放蒙太奇时角色应**向前移动**而非原地挥剑（原地动画 + Warp 只会做朝向修正，位置吸附无效）；
3. 如果发现只有朝向修正、没有位移：说明当前版本动画被裁成 In-Place，回到“确认攻击用带根运动的版本”再看 §7.1。

## 8. 参数速查与手感调法

| 想调整 | 方法 |
|---|---|
| **吸附更远/更近** | Warp 会把根位移终点对齐到目标；想要更贴近：把目标挂点从胸口换成 `spine_01`/根骨骼；或改攻击动画间奏（步进幅度） |
| **吸附太快/太猛** | 缩短 Warp 窗口（窗口小 = 同样位移在更短时间内完成，更急促）；加长窗口更柔和 |
| **吸附过头穿模** | 窗口终点提前（命中前 0.03 → 0.05）；或挂点换 `spine_02`（略靠后） |
| **只修正朝向、不吸位置** | 通知状态中选择仅朝向（Face/仅旋转模式） |
| **吸太远（隔空吸附）** | 攻击前置距离检查：超过 `AttackRange + 容差` 时跳过 AddOrUpdateWarpTarget（或通知状态按目标距离分支） |

## 9. 阶段验收

### 9.1 主角侧

1. 锁定 BOSS 后攻击：挥刀轨迹修正到 BOSS 胸口，前进距离与动画根位移一致；
2. BOSS 侧移时：攻击仍修正至目标（方向修正生效）；
3. 四段连击每段都吸附（第 2/3 段目标已很近时，位移量自然减少，不应穿模）；
4. 未锁定时：首版行为保持不变（无目标 → 无 WarpTarget → 原路数）；
5. 攻击被闪避/格挡/受击取消：吸附立即停止，角色不继续滑行；
6. 弹反/格挡/削韧不受影响（数值层无改动）。

### 9.2 BOSS 侧

1. 普攻/三连攻击玩家：吸附至玩家胸口；
2. 玩家移动时攻击仍命中（方向修正）；
3. 三连每段窗口生效；
4. 玩家受击/闪避躲开时：攻击未被取消则仍按原根位移前进（无目标 → 可改为仅朝向）。

### 9.3 混合回归

- 锁定移动、八向移动、闪避取消窗口、武器碰撞命中链、削韧、处决均按原有验收项回归。

## 10. 常见问题与排查

**10.1 编辑器里搜不到 `Motion Warping` 通知**

插件没启用成功：回 §4，确认 `.uproject` 插件项与 Build.cs 模块都已加，且重启过编辑器。

**10.2 只有朝向修正，没有位置吸附**

窗口内没有根位移：确认攻击动画是 Root-Motion 版本；若蒙太奇用的 In-Place 动画，位置吸附无效（这是最常见误区）。

**10.3 Log 里出现 “Warp window ... but no root motion” 类似警告**

同上：窗口内根采样为空。检查蒙太奇的根运动节是否被裁剪（复制期间丢掉了根曲线）。

**10.4 吸附了但方向不对（歪着冲）**

WarpTarget 挂点旋转问题：把目标挂点设为无旋转的中间骨骼（如 `spine_03` 而非 hand/socket）。

**10.5 吸附穿模 / 顶住目标滑步**

窗口终点没有提前：把 Warp 窗口终点挪到武器碰撞窗口前；或目标挂点后移一个骨骼。

**10.6 攻击取消后角色仍被拖着走一小段**

蒙太奇被打断时通知状态应立即结束；若仍有拖行，是 CharacterMovement 的残留速度所致——在取消路径（如 HandleMontageCancelled）里加一点 `StopMovementImmediately()`（不强制，首版先观察）。

**10.7 目标死亡/切换目标瞬间吸附跳变**

目标是 Weak 指针/黑版对象失效导致：每次攻击启动都重新设置 WarpTarget；攻击中目标中途死亡时，Warp 窗口停止吸附（属可接受行为）。

## 11. 本册刻意不做

- **非锁定目标的攻击吸附**（向前方搜索 Pawn）：首版只吸附“锁定目标 / BOSS TargetActor”，未锁定攻击维持原样；
- **自定义 WarpTarget Provider 蓝图**：先用组件 API 的 Actor + Socket 方案，够用；
- **转身吸附/背身攻击修正**：首版不处理目标在身后 180° 的情况；
- **吸附距离阈值曲线**：先固定“有目标就吸”，后续再按攻击需要给不同段配不同窗口。

## 12. 推荐实施顺序与编译闸门

```text
阶段 1：启用插件（.uproject + Build.cs）                 → 重启编辑器（闸门 1）
阶段 2：BaseCharacter 挂 MotionWarpingComponent          → 完整编译（闸门 2）
阶段 3：玩家/BOSS 攻击 GA 设置 WarpTarget                → 完整编译（闸门 3）
阶段 4：6 个攻击蒙太奇放 Motion Warping 通知状态（每段一个）
阶段 5：按 §9 验收
```

> 新增了组件（反射类型）与两个 GA 的调用，请完整编译后再测试，不要只用 Live Coding。
