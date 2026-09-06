# BOSS 三距离对峙与受击窗口实现指导

> 适用项目：`First`（UE 5.6 **中文版** / C++ / GAS / 内置 AI）
> 前置条件：BOSS 武器/拔剑/攻击（第一册）已完成；受击与死亡已完成
> 本册目标：
> - **Part A（D27/D28）**：BOSS 战斗中用三距离（最小 250 / 周旋 500 / 最大 800）驱动"后退 / 侧移 / 靠近"，形成对峙循环；
> - **Part B（D29）**：BOSS 只在攻击后摇的"受击窗口"内可被打断，防止被无限连击。

---

## 0. 先看结论（本册完成后得到什么）

```text
战斗开始后（已拔剑）：
  玩家距离 < 250       → BOSS 后退（拉开到周旋距离）
  玩家距离 250 ~ 500   → BOSS 侧移（绕玩家横向移动）
  玩家距离 > 500       → BOSS 靠近（追击）
  玩家进入攻击范围     → 攻击分支优先（普通攻击 / 三连）

受击规则：
  玩家命中 BOSS        → 掉血（任何情况都扣）
  命中时 BOSS 在后摇窗口 → 播放受击、打断当前招式（有约 0.8 秒冷却防无限连）
  命中时不在窗口        → 不掉节奏（霸体），只扣血
```

---

## 1. 设计说明（先读）

### 1.1 三距离定义（决策书 D27/D28）

| 距离 | 数值 | 行为 |
|---|---:|---|
| 最小距离 Min | 250（= 现有 AttackRadius） | 太近 → **后退** |
| 周旋距离 Strafe | 500（D28 已确认） | 中间 → **侧移** |
| 最大距离 Max | 800（= 现有 AlertRadius） | 太远 → **靠近** |

### 1.2 行为树总览（按情境分层，根层只挂三个大分支）

设计原则：**行为树按"情境"分层，而不是把所有行为平铺在根 Selector 下面**。
三距离对峙是"战斗中的移动逻辑"，所以放进"战斗"子树，和攻击并列；
根层只分三个大情境：**战斗 / 警戒 / 巡逻**。

```text
BT_Boss（根 = 选择器 Selector）
├─ 战斗分支（选择器 Selector）            ← 进入战斗后的所有逻辑都在这里
│   ├─ 攻击分支（序列，bInAttackRange）       → SelectAttack → Activate → Wait（对峙间隔）
│   ├─ 后退分支（序列，bPlayerTooClose）      → 算后退点 → MoveTo
│   ├─ 侧移分支（序列，bInStrafeRange && !bStrafeTimeout）→ 算侧移点 → MoveTo
│   └─ 靠近分支（序列，bPlayerTooFar || bStrafeTimeout）→ MoveTo(TargetActor)
├─ 警戒分支（序列，bInAlertRange && !bShouldChase） → Boss Alert（面向玩家/对峙）
└─ 巡逻分支（序列，无目标）                 → Find Patrol → MoveTo → Wait
```

每个子分支内部的装饰器不再重复写 `bShouldChase`——因为战斗分支自己已经要求了
`bShouldChase == true`，子树内部只需要判断距离。

> 侧移为什么需要停止条件：侧移分支本身没有"出口"，玩家只要一直待在中间距离，
> `算点 → 移动 → 成功 → 再算点` 就会无限循环。方案：侧移超过 **1.5 秒**（可配）后，
> 服务把 `bStrafeTimeout` 置 true → 侧移分支失效 → 靠近分支接管（逼近玩家进攻击范围 → 攻击），
> 形成"侧移 → 逼近 → 攻击 → 再对峙"的节奏。

### 1.3 受击窗口（D29）

- 新标签 `Boss.Status.HitReactWindow`，由攻击蒙太奇后摇段的 Notify 开/关；
- `GA_Boss_HitReact` 的 `ActivationRequiredTags` 要求该标签——**窗口外被打只扣血、不打断**；
- 受击能力加约 **0.8 秒冷却**（复用 `UFirstGE_BossCooldown`），防止窗口内连续命中无限僵直。

### 1.4 UE 5.6 说明

本册沿用已确认的 5.6 写法：能力标签统一 `SetAssetTags()`；`CooldownTags` 由派生类自己声明并覆写 `GetCooldownTags()`；`AbilityTriggers` 元素类型是 `FAbilityTriggerData`。

---

## 2. 本册改动总览（按文件找位置）

| 文件/资产 | 路径 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `MyGameplayTags.h/.cpp` | `Source/First/Public/`<br>`Source/First/Private/` | 修改 | 新增 3 个标签：`Boss.Status.Strafing`、`Boss.Status.HitReactWindow`、`Boss.Cooldown.HitReact` |
| `UBTService_UpdateCombatDistance.h/.cpp` | `Public/AI/`<br>`Private/AI/` | 修改 | 加 `StrafeRadius=500`；写 `bPlayerTooClose / bInStrafeRange / bPlayerTooFar` |
| `BTTask_BossGetStrafeLocation.h/.cpp` | `Public/AI/Tasks/`<br>`Private/AI/Tasks/` | 新建 | 算玩家左右侧移点 |
| `BTTask_BossGetBackOffLocation.h/.cpp` | 同上 | 新建 | 算远离玩家的后退点 |
| `UBTService_UpdateMoveSpeed.cpp` | `Private/AI/Services/` | 修改 | 侧移/后退 250，靠近 500 |
| `ANS_HitReactWindow.h/.cpp` | `Public/Notifies/`<br>`Private/Notifies/` | 新建 | 受击窗口 Notify（开/关标签） |
| `GA_Boss_HitReact.h/.cpp` | `Public/AbilitySystem/Abilities/BOSS/`<br>`Private/AbilitySystem/Abilities/BOSS/` | 修改 | 改继承 `UFirstBossGameplayAbility`；要求受击窗口；加冷却 |
| `BB_Boss` | `/Game/Enemy/AI/` | 修改 | 新增 5 个黑板键 |
| `BT_Boss` | `/Game/Enemy/AI/` | 修改 | 新增后退/侧移分支；追击分支改装饰器 |
| 攻击蒙太奇 | `/Game/Enemy/AnimBP/Montages/` | 配置 | 后摇段加 `Hit React Window` 通知状态 |

---

# Part A：三距离对峙（D27 / D28）

## 3. 第一步：新增标签

> 【改动位置】文件 `MyGameplayTags.h`
> 【新增内容】`Boss.Status.Strafing` 声明（动画/调试用；行为由黑板键驱动）

```cpp
	// 侧移周旋状态（以后方向混合动画可以用它切换表现）。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_Strafing);
```

> 【改动位置】文件 `MyGameplayTags.cpp`

```cpp
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_Strafing, "Boss.Status.Strafing");
```

编译闸门 1。

---

## 4. 第二步：扩展 UBTService_UpdateCombatDistance

> 【改动位置】文件 `UBTService_UpdateCombatDistance.h`
> 【新增内容】`StrafeRadius` 属性

**怎么找到位置**：打开 `UBTService_UpdateCombatDistance.h`，用搜索（`Ctrl+F`）搜：

```text
float AttackRadius = 250.f;
```

找到这一行后，在它**下面紧接着**粘贴：

```cpp
	// 周旋距离（D28）：中间距离 → 侧移。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float StrafeRadius = 500.f;
```

> 【改动位置】文件 `UBTService_UpdateCombatDistance.cpp`
> 【修改内容】多写三个对峙范围键；两个提前 return 分支也要清掉

按下面的顺序找位置（都用 `Ctrl+F` 搜）：

**改动 ①：没有目标时的提前返回**

搜索：

```text
if (!BossPawn || !Target)
```

你会看到这一段：

```cpp
	if (!BossPawn || !Target)
	{
		// 没有目标时清掉范围标志，确保行为树能回巡逻分支。
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);   ← 从这一行下面开始粘贴
		return;
	}
```

在 `bInAttackRange` 那一行之后、`return;` 之前，粘贴：

```cpp
		// 没有目标时，三个对峙判断也没有意义，全部清成 false（防止上一次战斗残留）。
		BB->SetValueAsBool(TEXT("bPlayerTooClose"), false);
		BB->SetValueAsBool(TEXT("bInStrafeRange"), false);
		BB->SetValueAsBool(TEXT("bPlayerTooFar"), false);
```

**改动 ②：目标不在 NavMesh 时的提前返回**

搜索：

```text
if (!bTargetOnNavMesh)
```

你会看到：

```cpp
	if (!bTargetOnNavMesh)
	{
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);   ← 从这一行下面开始粘贴
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		...
		return;
	}
```

同样在 `bInAttackRange` 那一行之后，粘贴**改动 ① 里的那三行**。

**改动 ③：正常计算距离后（函数末尾）**

搜索：

```text
Distance <= AttackRadius
```

你会看到函数末尾这一段：

```cpp
	BB->SetValueAsBool(TEXT("bInAlertRange"), Distance <= AlertRadius);
	BB->SetValueAsBool(TEXT("bInAttackRange"), Distance <= AttackRadius);   ← 从这一行下面开始粘贴
}
```

在 `bInAttackRange` 那一行之后、函数最后的 `}` 之前，粘贴：

```cpp
	// 三距离对峙（D27）：最小=AttackRadius，周旋=StrafeRadius，最大=AlertRadius。
	// 距离 < 250：太近，BOSS 应该后退。
	BB->SetValueAsBool(TEXT("bPlayerTooClose"), Distance < AttackRadius);
	// 250 <= 距离 <= 500：中间距离，BOSS 应该侧移绕圈。
	BB->SetValueAsBool(TEXT("bInStrafeRange"), Distance >= AttackRadius && Distance <= StrafeRadius);
	// 距离 > 500：太远，BOSS 应该靠近（追击）。
	BB->SetValueAsBool(TEXT("bPlayerTooFar"), Distance > StrafeRadius);
```

> 提醒：改动 ①、② 里加的是**清空**（false），改动 ③ 里加的是**写入真实判断**，三处都要加，别漏。

编译闸门 2。

---

## 5. 第三步：新建两个对峙定位任务

### 5.1 UBTTask_BossGetStrafeLocation（侧移点）

用编辑器向导创建（父类 **`BTTaskNode`**），类名 `BTTask_BossGetStrafeLocation`，文件放 `AI/Tasks/`，整体替换：

> 【改动位置】新建文件 `BTTask_BossGetStrafeLocation.h` / `BTTask_BossGetStrafeLocation.cpp`
> 【新增内容】以玩家为圆心、左右随机一侧、半径 = 周旋距离，计算侧移点写入黑板

`BTTask_BossGetStrafeLocation.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossGetStrafeLocation.generated.h"

/**
 * 计算玩家左右方向的侧移点（半径 = 周旋距离），写入黑板 StrafeLocation。
 */
UCLASS()
class FIRST_API UBTTask_BossGetStrafeLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossGetStrafeLocation();

	UPROPERTY(EditAnywhere, Category="Boss|Strafe")
	FName StrafeLocationKey = TEXT("StrafeLocation");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
```

`BTTask_BossGetStrafeLocation.cpp`：

```cpp
#include "AI/Tasks/BTTask_BossGetStrafeLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossGetStrafeLocation::UBTTask_BossGetStrafeLocation()
{
	NodeName = TEXT("Boss Get Strafe Location");
}

EBTNodeResult::Type UBTTask_BossGetStrafeLocation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	// 1. 先把要用到的东西从行为树/控制器/角色身上取出来。
	//    BB：黑板，用来读写键值；AIController：AI 控制器，能拿到它控制的 Pawn。
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	// 2. 缺任何一样都说明当前状态不对（比如目标已丢失），任务直接失败，
	//    行为树会退回上一个分支（例如回巡逻），而不是继续往下算。
	if (!BB || !Boss || !Target)
	{
		return EBTNodeResult::Failed;
	}

	// 3. 计算"从 BOSS 指向玩家"的单位方向（只取水平 X/Y，去掉高度差）。
	//    GetSafeNormal2D() 会把向量归一化为长度 1，同时防止零向量除零。
	const FVector ToTarget = (Target->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();

	// 4. 随机选左侧或右侧（每次侧移方向都可能变，BOSS 看起来更自然）。
	const float Side = FMath::RandBool() ? 1.f : -1.f;
	//    垂直向量公式：把一个方向向量 (X, Y) 旋转 90° 得到 (-Y, X) 或 (Y, -X)。
	//    用 Side 乘一下，就能在"左"和"右"之间切换。
	const FVector Perp(Side * -ToTarget.Y, Side * ToTarget.X, 0.f);

	// 5. 侧移点 = 玩家位置 + 垂直方向 × 周旋距离。
	//    意思是以玩家为圆心、半径 500 的圆上，挑一个左右两侧的点走过去，
	//    走过去的过程中 BOSS 和玩家的距离基本保持在 500 左右。
	const FVector StrafeLocation =
		Target->GetActorLocation() + Perp * Boss->GetStrafeRadius();

	// 6. 把算好的点写进黑板，后面的"移至（Move To）"节点会读这个键去移动。
	BB->SetValueAsVector(StrafeLocationKey, StrafeLocation);
	return EBTNodeResult::Succeeded;
}
```

> 上面用到了 `Boss->GetStrafeRadius()`，需要在 `BossCharacter.h` 加一个属性/访问器（见 5.3）。

### 5.2 UBTTask_BossGetBackOffLocation（后退点）

用编辑器向导创建（父类 **`BTTaskNode`**），类名 `BTTask_BossGetBackOffLocation`，整体替换：

> 【改动位置】新建文件 `BTTask_BossGetBackOffLocation.h` / `BTTask_BossGetBackOffLocation.cpp`
> 【新增内容】计算"远离玩家、落到周旋距离"的后退点写入黑板

`BTTask_BossGetBackOffLocation.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossGetBackOffLocation.generated.h"

/**
 * 计算远离玩家的后退点（落点距离玩家 = 周旋距离），写入黑板 BackOffLocation。
 */
UCLASS()
class FIRST_API UBTTask_BossGetBackOffLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossGetBackOffLocation();

	UPROPERTY(EditAnywhere, Category="Boss|BackOff")
	FName BackOffLocationKey = TEXT("BackOffLocation");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
```

`BTTask_BossGetBackOffLocation.cpp`：

```cpp
#include "AI/Tasks/BTTask_BossGetBackOffLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"

UBTTask_BossGetBackOffLocation::UBTTask_BossGetBackOffLocation()
{
	NodeName = TEXT("Boss Get Back Off Location");
}

EBTNodeResult::Type UBTTask_BossGetBackOffLocation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	// 和侧移任务一样：先把黑板、控制器、BOSS、目标取出来。
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	// 缺东西就直接失败，让行为树回退，不要硬算。
	if (!BB || !Boss || !Target)
	{
		return EBTNodeResult::Failed;
	}

	// 1. "远离玩家"的方向 = 玩家位置 - BOSS 位置 反过来，即 BOSS 指向玩家的反方向。
	FVector AwayDirection = (Boss->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	// 2. 如果 BOSS 和玩家几乎重合（方向向量接近 0），就退回"BOSS 面朝的方向"作为兜底。
	if (AwayDirection.IsNearlyZero())
	{
		AwayDirection = Boss->GetActorForwardVector();
		AwayDirection.Z = 0.f;
		AwayDirection.Normalize();
	}

	// 3. 后退落点 = 玩家位置 + 远离方向 × 周旋距离。
	//    效果：BOSS 从贴脸的位置，退到离玩家 500 的位置停下来（进入周旋距离）。
	const FVector BackOffLocation =
		Target->GetActorLocation() + AwayDirection * Boss->GetStrafeRadius();

	// 4. 写进黑板，后面的 Move To 节点读取并走过去。
	BB->SetValueAsVector(BackOffLocationKey, BackOffLocation);
	return EBTNodeResult::Succeeded;
}
```

### 5.3 ABossCharacter 增加 StrafeRadius

> 【改动位置】文件 `BossCharacter.h`
> 【新增内容】周旋距离属性与访问器

按下面的顺序找位置（都用 `Ctrl+F` 搜）：

**改动 ①：新增属性**

搜索：

```text
float PatrolRadius = 1200.f;
```

找到这一行后，在它**下面紧接着**粘贴：

```cpp
	// 周旋距离（决策书 D28）：对峙时侧移/后退的目标半径。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float StrafeRadius = 500.f;
```

**改动 ②：新增访问器**

搜索：

```text
FORCEINLINE float GetThreeComboDamagePerHit() const
```

找到这一行后，在它**下面**粘贴：

```cpp
	FORCEINLINE float GetStrafeRadius() const { return StrafeRadius; }
```

> 只要加在 `public:` 区域内就行；搜 `GetThreeComboDamagePerHit` 是为了给你一个准确的落点。

编译闸门 3。

### 5.4 新建 UBTService_BossStrafeTimer（侧移计时器，防无限绕圈）

用编辑器向导创建（父类 **`BTService`**，即 `UBTService`），类名 `BTService_BossStrafeTimer`（**不要带 U 前缀**），文件放 `AI/Services/`，整体替换：

> 【改动位置】新建文件 `UBTService_BossStrafeTimer.h` / `UBTService_BossStrafeTimer.cpp`
> 【新增内容】侧移计时：连续侧移超过 `StrafeMaxDuration` 秒，把黑板键 `bStrafeTimeout` 置 true；离开侧移范围或正在出招时清零

`UBTService_BossStrafeTimer.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_BossStrafeTimer.generated.h"

/**
 * 侧移计时器服务。
 * 挂在"战斗"分支上：只要 BOSS 处于侧移范围，就累计时间；
 * 超过 StrafeMaxDuration 后把黑板键 bStrafeTimeout 置 true，
 * 行为树据此停止侧移、转为靠近玩家（防止无限绕圈）。
 * 离开侧移范围或正在出招（bIsBusy）时计时清零。
 */
UCLASS()
class FIRST_API UBTService_BossStrafeTimer : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_BossStrafeTimer();

	// 侧移最长持续时间（秒）。超时后停止侧移，转为靠近。
	UPROPERTY(EditAnywhere, Category="Boss|Strafe", meta=(ClampMin="0.5"))
	float StrafeMaxDuration = 1.5f;

protected:
	// 每个 BOSS 实例的计时数据放在 NodeMemory 里，避免多个 BOSS 共用同一份计时。
	virtual uint16 GetInstanceMemorySize() const override;

	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
```

`UBTService_BossStrafeTimer.cpp`：

```cpp
#include "AI/Services/UBTService_BossStrafeTimer.h"

#include "BehaviorTree/BlackboardComponent.h"

// 每个 BOSS 实例自己的计时数据（放在任务内存里，而不是类的成员，
// 这样即使多个 BOSS 用同一个行为树，计时也不会互相干扰）。
struct FBossStrafeTimerMemory
{
	float ElapsedTime = 0.f;
};

UBTService_BossStrafeTimer::UBTService_BossStrafeTimer()
{
	NodeName = TEXT("Boss Strafe Timer");
}

uint16 UBTService_BossStrafeTimer::GetInstanceMemorySize() const
{
	return sizeof(FBossStrafeTimerMemory);
}

void UBTService_BossStrafeTimer::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	FBossStrafeTimerMemory* Memory = reinterpret_cast<FBossStrafeTimerMemory*>(NodeMemory);

	const bool bInStrafe = BB->GetValueAsBool(TEXT("bInStrafeRange"));
	// bIsBusy 是索敌手册里 UpdateBossState 聚合的"忙碌"键（出招/拔剑中）。
	// 如果你还没做 bIsBusy，就把这一行和下面的 && 条件删掉。
	const bool bBusy = BB->GetValueAsBool(TEXT("bIsBusy"));

	// 1. 不在侧移范围，或正在出招：计时清零，允许下次重新侧移。
	if (!bInStrafe || bBusy)
	{
		Memory->ElapsedTime = 0.f;
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		return;
	}

	// 2. 持续侧移：累计时间，超时置 true（行为树停止侧移、转为靠近）。
	Memory->ElapsedTime += DeltaSeconds;
	BB->SetValueAsBool(
		TEXT("bStrafeTimeout"),
		Memory->ElapsedTime >= StrafeMaxDuration);
}
```

编译闸门 4。

---

## 6. 第四步：UpdateMoveSpeed 联动（侧移/后退 250，靠近 500）

> 【改动位置】文件 `UBTService_UpdateMoveSpeed.cpp` 的 `TickNode`
> 【修改内容】靠近（bPlayerTooFar）才用奔跑，其余用步行

**怎么找到位置**：打开 `UBTService_UpdateMoveSpeed.cpp`，用 `Ctrl+F` 搜：

```text
const bool bShouldChase = BB->GetValueAsBool(TEXT("bShouldChase"));
```

你会看到 `TickNode` 末尾这 4 行（在 `if (!BB || !Boss)` 检查之后）：

```cpp
	// 追击中 = 奔跑（500）；否则巡逻 = 步行（250）。
	const bool bShouldChase = BB->GetValueAsBool(TEXT("bShouldChase"));
	Boss->SetMovementSpeed(
		bShouldChase ? Boss->ChaseMoveSpeed : Boss->PatrolMoveSpeed);
}
```

把这 4 行（注释 + 两行代码）**整体选中、替换**成下面的内容：

```cpp
	// 靠近玩家时奔跑（500）；后退/侧移/巡逻用步行（250）。
	// bChasing：是否在追击状态（索敌手册写入的黑板键）。
	const bool bChasing = BB->GetValueAsBool(TEXT("bShouldChase"));
	// bTooFar：玩家是否在周旋距离（500）之外，由 UpdateCombatDistance 服务写入。
	const bool bTooFar = BB->GetValueAsBool(TEXT("bPlayerTooFar"));
	// 只有在"追击中 且 距离确实远"时才用奔跑速度，其余情况一律步行。
	const bool bUseRunSpeed = bChasing && bTooFar;

	// 把选好的速度写进角色移动组件（MaxWalkSpeed）。
	Boss->SetMovementSpeed(
		bUseRunSpeed ? Boss->ChaseMoveSpeed : Boss->PatrolMoveSpeed);
```

编译闸门 5（可选但推荐）。

---

## 7. 第五步：黑板与行为树配置（Part A）

### 7.1 BB_Boss 新增键

| 键名 | 类型 | 说明 |
|---|---|---|
| `bPlayerTooClose` | Bool | 距离 < 250 |
| `bInStrafeRange` | Bool | 250 ≤ 距离 ≤ 500 |
| `bPlayerTooFar` | Bool | 距离 > 500 |
| `bStrafeTimeout` | Bool | 侧移超时（≥1.5 秒）→ 停止侧移、转为靠近 |
| `StrafeLocation` | Vector | 侧移目标点 |
| `BackOffLocation` | Vector | 后退目标点 |

### 7.2 BT_Boss 结构调整：把对峙放进"战斗"子树

**第一步：新建"战斗分支"（选择器 Selector）**

1. 在根节点（根 Selector）下新建一个子节点：**选择器（Selector）**，命名"战斗"；
2. 给"战斗"加装饰器：`bShouldChase == true`，**Abort = Both**（开始追击就进入战斗，脱战则整棵子树作废）；
3. 把现有的**攻击分支**整棵拖进"战斗"里面，作为它的第一个子节点（优先级最高）。

**第二步：在"战斗"里面新建三个对峙子分支**

在"战斗"内部、攻击分支之后依次添加（子分支装饰器都加 **Abort = Both**）：

```text
后退分支（序列 Sequence）
  装饰器：bPlayerTooClose == true
  1. Boss Get Back Off Location
  2. 移至（Move To）→ Blackboard Key = BackOffLocation，可接受半径 60

侧移分支（序列 Sequence）
  装饰器：bInStrafeRange == true && bStrafeTimeout == false
  1. Boss Get Strafe Location
  2. 移至（Move To）→ Blackboard Key = StrafeLocation，可接受半径 60

靠近分支（序列 Sequence）
  装饰器：bPlayerTooFar == true || bStrafeTimeout == true
  └─ 移至（Move To）→ Blackboard Key = TargetActor，可接受半径 180
```

注意：这三个子分支的装饰器**不用再写 `bShouldChase`**——战斗分支已经保证了，
子树内部只看距离就行。把原来的**追击分支**装饰器改成 `bPlayerTooFar == true || bStrafeTimeout == true`（就是"靠近分支"）。

**第三步：给"战斗"分支挂侧移计时器服务**

右键"战斗"分支 → **添加服务（Add Service）** → 选 `Boss Strafe Timer`，**间隔（Interval）**设 0.1～0.2。
这样侧移超时后 `bStrafeTimeout` 会置 true：侧移分支被挡住，靠近分支接管，BOSS 逼近玩家进攻击范围 → 攻击，不再无限绕圈。

> 攻击分支的冷却门控不用额外做：`Boss Select Attack` 在冷却中会返回失败，攻击分支自然不执行；
> 等冷却结束、玩家又在攻击范围内，攻击分支会按优先级（Abort）打断靠近/侧移。两个机制配合，
> 就是"侧移等待 → 冷却好/逼近后攻击"的节奏。

**最终结构检查**：根 Selector 下应该只有三个子节点：`战斗（Selector）`、`警戒（Sequence）`、`巡逻（Sequence）`。
战斗分支是一个完整的子树：`攻击 / 后退 / 侧移 / 靠近`。

> 第一版侧移/后退用默认移动朝向（角色朝移动方向），动画先用现有前向 Walk 占位；
> "面向玩家横移 + 方向混合动画"见第 15 节可选收尾。

---

# Part B：仅受击窗口可打断（D29）

## 8. 第六步：新增两个标签

> 【改动位置】文件 `MyGameplayTags.h`（在 `Boss_Status_Strafing` 附近）

```cpp
	// 受击窗口：攻击后摇期间可被打断。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Status_HitReactWindow);
	// 受击冷却：防止窗口内连续命中无限僵直。
	FIRST_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Boss_Cooldown_HitReact);
```

> 【改动位置】文件 `MyGameplayTags.cpp`

```cpp
	UE_DEFINE_GAMEPLAY_TAG(Boss_Status_HitReactWindow, "Boss.Status.HitReactWindow");
	UE_DEFINE_GAMEPLAY_TAG(Boss_Cooldown_HitReact, "Boss.Cooldown.HitReact");
```

编译闸门 6。

---

## 9. 第七步：新建 UFirstANS_HitReactWindow（受击窗口 Notify）

用编辑器向导创建（父类 **`AnimNotifyState`**），类名 `ANS_HitReactWindow`，文件放 `Notifies/`，整体替换：

> 【改动位置】新建文件 `ANS_HitReactWindow.h` / `ANS_HitReactWindow.cpp`
> 【新增内容】Begin 加 `Boss.Status.HitReactWindow`，End 移除

`ANS_HitReactWindow.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_HitReactWindow.generated.h"

/**
 * 受击窗口（Hit React Window）。
 * 放在 BOSS 攻击蒙太奇的后摇段：窗口内受击才会打断（配合 GA_Boss_HitReact 的 RequiredTags）。
 */
UCLASS(meta = (DisplayName = "Hit React Window"))
class FIRST_API UFirstANS_HitReactWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
```

`ANS_HitReactWindow.cpp`：

```cpp
#include "Notifies/ANS_HitReactWindow.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BaseCharacter.h"
#include "MyGameplayTags.h"

void UFirstANS_HitReactWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 通知状态（Notify State）是动画资产里的一个"时间段"：
	// 动画播放到这段的开始，引擎自动调用 NotifyBegin；离开这段，自动调用 NotifyEnd。
	// 这里先拿到动画所属的角色，再拿到它的 ASC（GAS 组件）。
	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	// 拿不到 ASC 说明角色没有 GAS（正常不会发生，但防御性判空更安全）。
	if (!ASC)
	{
		return;
	}

	// 进入受击窗口：给 ASC 加一个 Loose Tag。
	// 之后玩家命中 BOSS 时，GA_Boss_HitReact 检查到这个标签才允许打断。
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_HitReactWindow);
}

void UFirstANS_HitReactWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	// 离开受击窗口：同样先取角色和 ASC。
	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	// 防御性判空。
	if (!ASC)
	{
		return;
	}

	// 移除标签：窗口结束，之后命中不再打断（只扣血）。
	ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_HitReactWindow);
}
```

编译闸门 7。

---

## 10. 第八步：改造 GA_Boss_HitReact（要求窗口 + 加冷却）

### 10.1 头文件

> 【改动位置】文件 `GA_Boss_HitReact.h`
> 【修改内容】父类改为 `UFirstBossGameplayAbility`；新增 `CooldownTags` 与 `GetCooldownTags` 覆写

按下面的顺序找位置（都用 `Ctrl+F` 搜）：

**改动 ①：替换 include**

搜索：

```text
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
```

把这一整行替换成：

```cpp
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"
```

**改动 ②：替换继承的父类**

搜索：

```text
class FIRST_API UGA_Boss_HitReact : public UFirstGameplayAbility
```

把这行改成：

```cpp
class FIRST_API UGA_Boss_HitReact : public UFirstBossGameplayAbility
```

**改动 ③：新增 CooldownTags 声明**

搜索：

```text
void FinishHitReact(bool bWasCancelled);
```

在这行下面粘贴：

```cpp
	// UE 5.6：基类已移除 CooldownTags，自己声明并覆写 GetCooldownTags。
	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
```

### 10.2 源文件

> 【改动位置】文件 `GA_Boss_HitReact.cpp`
> 【新增内容】要求受击窗口标签；配置受击冷却

按下面的顺序找位置（都用 `Ctrl+F` 搜）：

**改动 ①：新增 include**

搜索：

```text
#include "MyGameplayTags.h"
```

在它上面（include 区里）加一行：

```cpp
#include "AbilitySystem/GameplayEffects/FirstGE_BossCooldown.h"
```

**改动 ②：构造函数加"要求窗口 + 冷却"**

搜索：

```text
ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);
```

你会看到构造函数里这几行：

```cpp
	// 受击期间短暂硬直；不能叠加触发；死亡后不再受击。
	ActivationOwnedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Boss_Status_Staggered);
	ActivationBlockedTags.AddTag(MyGameplayTags::Shared_Status_Dead);   ← 从这一行下面开始粘贴
```

在 `Shared_Status_Dead` 那一行下面粘贴：

```cpp
	// 只有攻击后摇的受击窗口内才可被打断（D29）。
	// ActivationRequiredTags：激活前置条件，身上没有这个标签，能力就不会激活。
	ActivationRequiredTags.AddTag(MyGameplayTags::Boss_Status_HitReactWindow);

	// 受击冷却：防止窗口内连续命中无限僵直。
	// 冷却 GE、冷却标签、冷却时长三者配合，GAS 会自动阻止冷却期间再次激活。
	CooldownGameplayEffectClass = UFirstGE_BossCooldown::StaticClass();
	CooldownTags.AddTag(MyGameplayTags::Boss_Cooldown_HitReact);
	CooldownDuration = 0.8f;
```

**改动 ③：文件末尾新增 GetCooldownTags 实现**

翻到文件最末尾（`FinishHitReact` 函数的最后一个 `}` 之后），粘贴：

```cpp
// UE 5.6：基类已移除 CooldownTags 成员，这里返回我们自己声明的那个。
// GAS 检查冷却时会调用这个函数，拿到"哪些标签代表正在冷却"。
const FGameplayTagContainer* UGA_Boss_HitReact::GetCooldownTags() const
{
	return &CooldownTags;
}
```

编译闸门 8。

> 效果说明：掉血事件仍然每次都发，但受击能力只有在"窗口标签存在 + 不在受击冷却"时才会激活——窗口外被打只扣血，BOSS 不掉节奏。

---

## 11. 第九步：攻击蒙太奇加受击窗口

对 `AM_Boss_Attack_Normal` 和 `AM_Boss_Attack_Combo` 分别操作：

1. 双击打开攻击蒙太奇；
2. 通知轨道右键 → **添加通知状态（Add Notify State）** → **Hit React Window**；
3. 把窗口放在**后摇段**（推荐：最后 1/3 到收刀前；前摇和有效帧不要放）；
4. 保存。

> 手感调优：窗口越短，BOSS 越难被打断；先放后 1/3，再按实际节奏微调。

---

## 12. 编译顺序汇总

```text
闸门 1：MyGameplayTags（Boss.Status.Strafing）
闸门 2：UBTService_UpdateCombatDistance（StrafeRadius + 三键）
闸门 3：BTTask_BossGetStrafeLocation + BTTask_BossGetBackOffLocation + ABossCharacter::StrafeRadius
闸门 4：UBTService_BossStrafeTimer（侧移计时器，防无限绕圈）
闸门 5：UBTService_UpdateMoveSpeed 联动（可选）
闸门 6：MyGameplayTags（HitReactWindow + Cooldown.HitReact）
闸门 7：UFirstANS_HitReactWindow
闸门 8：GA_Boss_HitReact 改造
最后：编辑器配置（BB 键 / BT 分支 / 蒙太奇窗口）
```

每完成一个闸门做一次普通 Build。

---

## 13. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 后退 | 贴脸打 BOSS 后拉开 | BOSS 后退到约 500 距离，不穿墙 |
| 侧移 | 站在 300~450 距离 | BOSS 左右横向移动绕圈 |
| 侧移有尽 | 站在 400 距离不动 | 侧移约 1.5 秒后转为靠近，进攻击范围后攻击，不无限绕圈 |
| 靠近 | 站到 600+ | BOSS 加速靠近（500 奔跑） |
| 攻击优先 | 侧移时进 250 内 | 攻击分支接管，正常出招 |
| 出招暂停计时 | 侧移中 BOSS 出招/拔剑 | 计时清零，出招后重新侧移（有 bIsBusy 时生效） |
| 窗口外被打 | 非后摇命中 BOSS | 掉血但不打断（BOSS 继续动作） |
| 窗口内被打 | 后摇段命中 | 受击动画 + 打断招式 |
| 防无限连 | 窗口内连打 | 第一次打断后约 0.8 秒内不再二次受击 |
| 战斗循环 | 观察 1 分钟 | 后退/侧移/靠近/攻击交替，不死锁 |

---

## 14. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| BOSS 不侧移/不后退 | 服务没挂到根节点、键名不一致、或分支装饰器错 | 检查 7.1 / 7.2，键名与 C++ 完全一致 |
| 一直侧移不攻击 | 侧移分支装饰器条件太宽 | 侧移分支必须同时要求 `bInStrafeRange && !bStrafeTimeout`；攻击分支优先级最高 |
| 侧移穿墙/卡墙 | 目标点是直接算的，没做导航 | 第一版接受；扩展时用导航投影（参考服务里的 ProjectPointToNavigation） |
| BOSS 无限侧移绕圈 | 侧移计时器没挂到战斗分支 / `bStrafeTimeout` 键没建 / 装饰器没加 | 检查 5.4、7.1、7.2 第三步：服务挂战斗分支、键名一致、侧移装饰器加 `!bStrafeTimeout` |
| 怎么打都不打断 | 蒙太奇没放 Hit React Window，或 RequiredTags 没加 | 检查 11 与 10.2 |
| 永远被连死 | 受击没冷却 | 检查 10.2 的 CooldownDuration 与 CooldownTags |
| 窗口外也打断 | 旧版 HitReact 没改 RequiredTags | 确认 `ActivationRequiredTags` 已加 `Boss.Status.HitReactWindow` |

---

## 15. 后续扩展

1. **面向玩家横移（表现收尾）**：侧移分支用 `SetFocus(玩家)` + MoveTo 勾选 **AllowStrafe**，并临时开 `bUseControllerRotationYaw`；ABP 按 `Boss.Status.Strafing` 切换 `Anim_DKF_Walk_Left/Right` 方向动画；
2. **韧性叠加（Phase 3）**：D29 的窗口 + 受击冷却上叠加"削韧 → 50% 大僵直 → 归零处决"（A3~A7），窗口决定"能不能打断"，韧性决定"打断到什么程度"；
3. **EQS 侧移点**：点数多、地形复杂时用 EQS（`EQS_FindStrafingLocation` 思路）代替手算向量；
4. **读指令（后续）**：对峙/侧移期间监听玩家攻击/喝药，提前结束周旋发起进攻。
