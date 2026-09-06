# BOSS 待机与移动动画装配实现指导（DK 资源包）

> 适用项目：`First`（UE 5.6 **中文版** / C++ / GAS / 内置 AI）
> 前置条件：BOSS 巡逻已经跑通（BP_Boss 能在地图上移动）
> 本册包含两部分：① ABP 三状态机装配（纯资产操作）；② 巡逻 250 / 追击 500 双速度切换（需要改少量 C++，已标明位置）

---

## 0. 先看结论（本册完成后得到什么）

```text
BOSS 站着不动    → Idle 状态（Anim_DKF_Idle）
BOSS 前往巡逻点  → Walk 状态（Anim_DKF_Walk_Fwd，移动速度 250）
BOSS 追击玩家    → Run 状态（Anim_DKF_Run_Fwd，移动速度 500）
```

和主角的 ABP 一样，用**状态机**组织动画：`Idle / Walk / Run` 三个状态，按 `GroundSpeed` 自动切换。

---

## 1. 前置判断：为什么用 DKF 动画（先读这一节）

你的 `BP_Boss` 当前使用的网格体是 **`SKM_DKF_Full`**（Dark Knight 女骑士），它的骨架是 **`SK_DKF_Full`**。

因此：

```text
动画直接用同骨架的 Anim_DKF_* 系列 → 不需要 IK 重定向
不要再给 BOSS 用玩家的 ABP_DK_Sword（那是另一套骨架 SK_Mannequin_Skeleton，会错乱）
```

如果你其实想让 BOSS 用男骑士：把网格体换成 `SKM_DKM_Full`、动画换成 `Anim_DKM_*` 即可，本手册每一步都一样。

> 后续建议：BOSS 的攻击（`Anim_DKF_Attack_01~03`）、受击（`Anim_DKF_Hit_*`）、死亡（`Anim_DKF_Death`）都在同一个包里、同一套骨架，以后直接可用；Katana / SwordAnimsetPro 的动画是另一套骨架，需要 IK 重定向，能少用就少用。

---

## 2. 本册改动总览（按文件/资产找位置）

| 文件/资产 | 路径 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `ABP_Boss` | `/Game/Enemy/AnimBP/` | 新建 | 动画蓝图：父类 `FirstDKAnimInstance`，目标骨架 `SK_DKF_Full` |
| `ABP_Boss` 动画图 | — | 修改 | 三状态机 `Idle / Walk / Run` + `DefaultSlot` 插槽 |
| `BossCharacter.h` | `Source/First/Public/Character/` | 修改 | 新增 `PatrolMoveSpeed=250`、`ChaseMoveSpeed=500`、`SetMovementSpeed()` 声明 |
| `BossCharacter.cpp` | `Source/First/Private/Character/` | 修改 | 构造函数改用 `PatrolMoveSpeed`；新增 `SetMovementSpeed()` 实现 |
| `UBTService_UpdateMoveSpeed.h/.cpp` | `Source/First/Public/AI/Services/`<br>`Source/First/Private/AI/Services/` | 新建 | 按 `bShouldChase` 切换 250 / 500 |
| `BB_Boss` | `/Game/Enemy/AI/` | 修改 | 新增黑板键 `bShouldChase`（Bool） |
| `BT_Boss` | `/Game/Enemy/AI/` | 修改 | 根节点挂载 `Update Move Speed` 服务 |
| `BP_Boss` | `/Game/Enemy/BP_Boss` | 修改 | 动画类（Anim Class）= `ABP_Boss` |
| `BS_Boss_Locomotion` | `/Game/Enemy/AnimBP/` | 可选 | 本册三状态机用不到，以后做八向移动再启用 |

> 源动画素材（只读，不改原件）：`/Game/Dark_Knight/Dark_Knight_Female/Animations/Anim_DKF_Idle`、`Anim_DKF_Walk_Fwd`、`Anim_DKF_Run_Fwd`

---

## 3. 第一步：确认 C++ 动画实例的变量（不用改代码）

你的 `UFirstDKAnimInstance`（C++ 动画实例）已经提供了状态机过渡要用的变量：

| 变量 | 类型 | 含义 |
|---|---|---|
| `GroundSpeed` | Float | 水平移动速度，状态机过渡条件用它 |
| `bHasAcceleration` | Bool | 当前是否有移动加速度 |
| `bShouldMove` | Bool | 是否应该进入移动表现 |

`ABP_Boss` 的父类选 `FirstDKAnimInstance` 后，这些变量自动可用。

---

## 4. 第二步（可选）：创建 BS_Boss_Locomotion 混合空间

本册的三状态机**直接播放单段动画，不需要混合空间**。如果你已经按旧版手册建了 `BS_Boss_Locomotion`，放着不碍事，以后做八向移动时再启用。

如果以后要用，创建方法（先记着即可）：

```text
/Game/Enemy/AnimBP/ 右键 → 动画 → 混合空间 1D（Blend Space 1D）
骨架选 SK_DKF_Full → 轴 GroundSpeed 0~600
采样：0=Anim_DKF_Idle  200=Anim_DKF_Walk_Fwd  500=Anim_DKF_Run_Fwd
```

---

## 5. 第三步：创建 ABP_Boss 三状态机（和主角 ABP 同结构）

### 5.1 新建动画蓝图

1. 在 `/Game/Enemy/AnimBP/` 右键 → **动画（Animation）→ 动画蓝图（Animation Blueprint）**；
2. **父类（Parent Class）**搜索并选择 `FirstDKAnimInstance`（C++ 类，不要选默认的 AnimInstance）；
3. **目标骨架（Target Skeleton）**选择 `SK_DKF_Full`；
4. 命名 `ABP_Boss`，保存。

> 【改动位置】新建文件 `ABP_Boss`
> 【新增内容】动画蓝图本体；父类选 `FirstDKAnimInstance` 才能在过渡条件里看到 `GroundSpeed`

### 5.2 添加状态机与三个状态

1. 双击打开 `ABP_Boss`，切到**动画图（Anim Graph）**标签；
2. 右键空白处 → **添加状态机（Add State Machine）**，命名 `Locomotion`；
3. 双击 `Locomotion` 节点进入状态机内部；
4. 右键创建三个状态（**添加状态 / Add State**）：`Idle`、`Walk`、`Run`；
5. 分别双击每个状态，把对应动画连到输出姿势：

| 状态 | 动画 |
|---|---|
| `Idle` | `Anim_DKF_Idle` |
| `Walk` | `Anim_DKF_Walk_Fwd` |
| `Run` | `Anim_DKF_Run_Fwd` |

6. 返回状态机，右键状态 → **添加过渡（Add Transition）**，按下面的表连好所有过渡。

### 5.3 添加过渡条件（按 GroundSpeed 切换）

| 过渡 | 条件 | 说明 |
|---|---|---|
| Idle → Walk | `GroundSpeed > 20` | 开始移动 |
| Idle → Run | `GroundSpeed > 375` | 起步就是奔跑 |
| Walk → Run | `GroundSpeed > 375` | 250（巡逻）→ 500（追击） |
| Run → Walk | `GroundSpeed < 375` | 减速回步行 |
| Walk → Idle | `GroundSpeed < 20` | 停下 |
| Run → Idle | `GroundSpeed < 20` | 停下 |

设置方法：点中过渡箭头 → 右侧**细节面板 → 过渡规则（Transition Rule）** → 添加条件，选变量 **`GroundSpeed`** + 比较符（大于/小于）+ 阈值；**过渡时间（Transition Time）**填 `0.2`，让切换平滑。

> 提示：如果条件列表里找不到 `GroundSpeed`，说明父类没选对——回去检查 5.1 的父类是不是 `FirstDKAnimInstance`。

### 5.4 接上 DefaultSlot（为以后攻击蒙太奇预留）

1. 回到动画图，右键空白处 → **添加插槽（Add Slot）**，名称填 `DefaultSlot`；
2. 连接改为：`Output Pose ← DefaultSlot ← Locomotion 状态机`；
3. **编译（Compile）**并**保存（Save）**。

> 最终结构：
>
> ```text
> Output Pose ← DefaultSlot ← Locomotion（Idle / Walk / Run 状态机）
> ```

---

## 6. 第四步：巡逻 250 / 追击 500 双速度切换（C++）

要让 BOSS"去巡逻点 250 步行、追玩家 500 奔跑"，需要让它能动态改速度。做法：BOSS 角色保存两个速度，行为树用一个服务按 `bShouldChase` 切换。

### 6.1 修改 ABossCharacter（新增两个速度 + 设置函数）

> 【改动位置】文件 `BossCharacter.h`，类的 `public:` 区域
> 【新增内容】`PatrolMoveSpeed`、`ChaseMoveSpeed` 两个属性；`SetMovementSpeed()` 函数声明

在 `BossCharacter.h` 的 `public:` 区域新增：

```cpp
	// 前往巡逻点时的移动速度：步行。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float PatrolMoveSpeed = 250.f;

	// 追击玩家时的移动速度：奔跑。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float ChaseMoveSpeed = 500.f;

	// 统一设置当前移动速度（巡逻 250 / 追击 500 由行为树服务调用）。
	void SetMovementSpeed(float NewSpeed);
```

> 【改动位置】文件 `BossCharacter.cpp`
> 【修改内容】构造函数里原来的 `MaxWalkSpeed = 500.f` 改成 `PatrolMoveSpeed`
> 【新增内容】文件末尾新增 `SetMovementSpeed()` 实现

构造函数改一行：

```cpp
	// 巡逻与追击共用同一个速度的旧方案取消：现在默认步行，追击由服务切到奔跑。
	GetCharacterMovement()->MaxWalkSpeed = PatrolMoveSpeed;
```

文件末尾新增：

```cpp
void ABossCharacter::SetMovementSpeed(float NewSpeed)
{
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = NewSpeed;
	}
}
```

### 6.2 新建 UBTService_UpdateMoveSpeed（速度切换服务）

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索父类 `BTService`（即 `UBTService`）；
3. 类名填写 `BTService_UpdateMoveSpeed`（**不要带 U 前缀**，向导会自动加 U）；
4. 文件放到 `Source/First/Public/AI/Services` 与 `Source/First/Private/AI/Services`，用下面内容整体替换（**新建文件，内容全部为新增**）。

> 【改动位置】新建文件 `UBTService_UpdateMoveSpeed.h`
> 【新增内容】`TickNode()` 声明

`UBTService_UpdateMoveSpeed.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_UpdateMoveSpeed.generated.h"

/**
 * 每帧（按 Interval 节流）根据黑板键 bShouldChase 切换 BOSS 移动速度：
 *   巡逻移动（bShouldChase == false）→ PatrolMoveSpeed（250，Walk）
 *   追击     （bShouldChase == true） → ChaseMoveSpeed（500，Run）
 *   警戒 / 巡逻停留 / 攻击对峙 → 0
 */
UCLASS()
class FIRST_API UBTService_UpdateMoveSpeed : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateMoveSpeed();

protected:
	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
```

> 【改动位置】新建文件 `UBTService_UpdateMoveSpeed.cpp`
> 【新增内容】`TickNode()` 完整实现

`UBTService_UpdateMoveSpeed.cpp`：

```cpp
#include "AI/Services/UBTService_UpdateMoveSpeed.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTService_UpdateMoveSpeed::UBTService_UpdateMoveSpeed()
{
	NodeName = TEXT("Update Move Speed");
}

void UBTService_UpdateMoveSpeed::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!BB || !Boss)
	{
		return;
	}

	const bool bShouldChase = BB->GetValueAsBool(TEXT("bShouldChase"));
	const bool bInAlertRange = BB->GetValueAsBool(TEXT("bInAlertRange"));
	const bool bInAttackRange = BB->GetValueAsBool(TEXT("bInAttackRange"));
	const bool bShouldStop = BB->GetValueAsBool(TEXT("bShouldStop"));

	float TargetSpeed = Boss->ChaseMoveSpeed;
	if (bInAttackRange)
	{
		// 玩家进入攻击范围：停下对峙（攻击分支当前是占位，速度归零）。
		TargetSpeed = 0.f;
	}
	else if (bShouldChase)
	{
		// 追击中 = 奔跑（500，Run）。
		TargetSpeed = Boss->ChaseMoveSpeed;
	}
	else if (bInAlertRange || bShouldStop)
	{
		// 警戒对峙 / 巡逻点停留：速度归零，角色完全停下。
		TargetSpeed = 0.f;
	}
	else
	{
		// 前往巡逻点 = 步行（250，Walk）。
		TargetSpeed = Boss->PatrolMoveSpeed;
	}

	Boss->SetMovementSpeed(TargetSpeed);
}
```

### 6.3 黑板与行为树配置

1. 打开 `BB_Boss`，确认已有黑板键 **`bShouldChase`（Bool）**，并新增 **`bShouldStop`（Bool）**（巡逻点停留标志）；
2. 打开 `BT_Boss`，在**根节点**上右键 → **添加服务（Add Service）** → 选 `Update Move Speed`，把**间隔（Interval）**设为 0.2；
3. 打开 `BT_Boss`，把**巡逻分支**调整为下面的结构（用引擎自带的 **Set Bool Key** 任务切换停留标志）：

```text
巡逻分支（序列 Sequence）
  1. Set Bool Key：bShouldStop = false   ← 先清掉上一轮可能残留的“停止”
  2. Find Patrol Location
  3. 移至（Move To）→ PatrolLocation
  4. Set Bool Key：bShouldStop = true    ← 到达巡逻点，进入停留
  5. 等待（Wait）→ 5.0 秒                ← 停留时长按需求改为 5 秒
```

> Set Bool Key 是引擎自带任务：右键巡逻分支 → **新建任务（Add Task）** → 搜索 **Set Bool Key**；
> 在细节面板里把 **黑板键（Blackboard Key）** 选为 `bShouldStop`，**值（Value）** 勾选/取消即可。
> 放在序列开头是为了防止“巡逻等待被打断后，bShouldStop 残留为 true，导致回巡逻后站在原地不动”。

4. 现在速度规则自动变为：

| 状态 | bShouldChase | bInAttackRange | bShouldStop | 速度 |
|---|---:|---:|---:|---:|
| 巡逻移动 | false | false | false | 250（Walk） |
| 巡逻停留 | false | false | true | 0 |
| 警戒对峙 | false | false | 任意 | 0（站着转身） |
| 追击 | true | false | 任意 | 500（Run） |
| 攻击范围 | 任意 | true | 任意 | 0（停下，等待攻击手册） |

5. `BossAIController::InitializeBossBlackboard()` 里最好补一行 `BB->SetValueAsBool(TEXT("bShouldStop"), false);`，保证出生时标志干净（不改也一般没问题，Bool 键默认就是 false）。

### 6.4 编译

这次改了 C++，必须做一次普通 Build（不要只依赖 Live Coding）。

---

## 7. 第五步：配置 BP_Boss

1. 打开 `BP_Boss` → **类默认值（Class Defaults）**；
2. 检查：
   - **网格体（Mesh）**：`SKM_DKF_Full`（保持现状）；
   - **动画类（Anim Class）**：改成 `ABP_Boss`；
   - **角色移动 → 最大行走速度（Max Walk Speed）**：留 `500` 即可（运行时会由速度服务覆盖成 250 或 500）；
3. 编译并保存蓝图。

---

## 8. 第六步：根运动检查（防止滑步）

运行 PIE，让 BOSS 巡逻，观察脚下：

- 走路/跑步**正常贴地** → 什么都不用做；
- 走路时**脚在地面滑动 / 身体漂移** → 说明源动画带根运动，需要处理：
  1. 内容浏览器里把 `Anim_DKF_Walk_Fwd`、`Anim_DKF_Run_Fwd` **各复制一份**到 `/Game/Enemy/AnimBP/Source/`（不要改原件）；
  2. 打开副本 → **资产细节 → 根运动（Root Motion）**；
  3. 勾选 **强制根锁定（Force Root Lock）**，或把“启用根运动（Enable Root Motion）”设为关闭；
  4. 把状态机里 `Walk`、`Run` 状态用的动画换成这两份副本，保存。

> 小知识：移动动画如果是"原地循环"（脚步原地踏步），就直接放进状态机给角色移动组件驱动；如果是"带位移的根运动"，必须锁根，否则动画位移和角色移动会叠加，造成漂移。

---

## 9. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 待机 | BOSS 静止不动 | `Idle` 状态，播放 `Anim_DKF_Idle`，不 T 型 |
| 巡逻走路 | 观察 BOSS 前往巡逻点 | `Walk` 状态，移动速度 250 |
| 巡逻停留 | 到达巡逻点后观察 5 秒 | 停下约 5 秒再出发；停留期间移动速度 0、播放 `Idle` |
| 追击奔跑 | 完成索敌后，玩家被追击 | `Run` 状态，移动速度 500 |
| 攻击对峙 | 玩家进入攻击范围（250） | 停下并面向玩家，移动速度 0（攻击分支当前是占位） |
| 停下 | 让 BOSS 停止 | 平滑回到 `Idle`，不抖动 |
| 过渡 | 加速/减速过程 | 状态切换平滑（0.2 秒），不跳变 |
| 朝向 | 观察巡逻转弯 | 角色面向移动方向 |
| 滑步 | 观察脚底 | 不滑动、不漂移 |
| 保存后重开 | 重启编辑器 | ABP / 服务引用不丢失 |

---

## 10. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| BOSS 是 T 型 / 动画完全不播放 | 动画类没设，或骨架不匹配 | BP_Boss 的 Anim Class 必须是 `ABP_Boss`；ABP_Boss 的目标骨架必须是 `SK_DKF_Full` |
| 状态机一直是 Idle，不动画 | 过渡条件里找不到 `GroundSpeed` | 父类没选 `FirstDKAnimInstance`，回去改 5.1 |
| 一直 Walk，永远不 Run | 追击没做，`bShouldChase` 恒为 false | 这是预期的：先完成索敌手册的追击分支 |
| 速度一直是 500 / 250 不变 | 速度服务没挂到根节点，或 `bShouldChase` 键没建 | 检查 6.3：服务挂根节点、键名一致 |
| 追击时偶尔原地不动 | 玩家进入攻击范围（当前攻击分支是占位，停下是预期）；或追击分支 Move To 的可接受半径太小 / 目标不可达 | 先用行为树调试器看卡住时是哪个分支高亮：攻击分支高亮 → 属预期占位；Move To 高亮或失败 → 检查可接受半径（建议 180）与 NavMesh 覆盖 |
| 回巡逻后站在原地不动 | 巡逻等待被中断时 `bShouldStop` 残留为 true | 确认巡逻分支开头有 `Set Bool Key：bShouldStop = false`（见 6.3） |
| 走路滑步/漂移 | 源动画带根运动 | 按第 8 步复制并强制根锁定 |
| 状态切换跳变 | 过渡时间太短 | 过渡时间填 0.2 秒以上 |
| 编译报 C4430：类名和构造函数名不一致 | 新建服务类时名字带了 U 前缀 | 统一类名，去掉多余前缀（见 6.2） |

---

## 11. 后续扩展

1. **警戒待机**：包里还有 `Anim_DKF_Idle_Alert`（警戒姿势），以后在状态机里加一个 `Alert` 状态，用 GAS Tag 或黑板键驱动切换；
2. **八向移动**：`Anim_DKF_Walk_*` 有 8 个方向、`Run_*` 有 3 个方向，等需要侧移周旋（决策书 D28）时做方向混合；
3. **攻击 / 受击 / 死亡**：`Anim_DKF_Attack_01~03`、`Anim_DKF_Hit_*`、`Anim_DKF_Death` 已在包里，随攻击手册、受击手册接入（`DefaultSlot` 已预留）。
