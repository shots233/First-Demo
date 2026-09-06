# BOSS 索敌与警戒实现指导

> 适用项目：`First`（UE 5.6 / C++ / GAS / 内置 AI）
> 引擎版本：Unreal Engine 5.6 **中文版**；文中编辑器操作按中文版界面名称标注，括号内为英文名称。
> 前置条件：先完成《BOSS 基本巡逻实现指导》，本册在 `ABossAIController`、`BT_Boss` 基础上扩展。
> 本文只提供实现指导，不会自动修改你的 C++、蓝图或动画资源。
> ⚠️ **初学者必读**：本文里的代码已经是 UE 5.6 修正后的正确版本。还没创建文件的同学直接照抄；已经按旧版本抄过代码的同学，请先翻到文末 **第 14 节：UE 5.6 修正对照**，按表改好自己的文件再编译。

---

## 0. 先看结论（本册完成后得到什么）

BOSS 的感知由三个同心范围组成（所有数值都可在蓝图/行为树里调）：

```text
攻击范围 AttackRadius（默认 250）
  玩家进入 → 进入“攻击分支”
  本册先占位：BOSS 停下并面向玩家；真正的攻击 + 对峙循环在《BOSS 轻攻击/重攻击实现指导》实现

警戒范围 AlertRadius（默认 800）
  玩家在攻击范围外、警戒范围内 → 触发警戒
  BOSS 转向玩家、对峙计时（默认 3 秒）
  对峙超时 → 发起追击

巡逻范围 PatrolRadius（默认 1200，上一册）
  玩家不在警戒范围内 → 继续巡逻
```

视觉感知参数（AIPerception）：

| 参数 | 默认值 | 含义 |
|---|---|---|
| SightRadius | 1000 | 最远看多远（决策书 D3 建议 800～1200） |
| PeripheralVisionAngleDegrees | 60 | 左右各 60°，共 120° 视野（决策书 D4） |
| LoseSightRadius | 1400 | 走出这个距离才开始判定“看不到” |
| MaxAge | 5 秒 | 成功视觉刺激的最大记忆年龄；不是“看不见 5 秒后才触发失败回调” |

一句话：**BOSS 平时巡逻 → 玩家进警戒范围转头对峙 → 对峙超时进入锁定战斗 → 视野不再决定脱战，领地边界负责结束战斗。**

---

## 1. 本次要解决的问题

1. 用什么发现玩家 → `UAIPerceptionComponent` 视觉感知；
2. 三个范围怎么切换 → 距离服务每 0.2 秒算一次距离写黑键；
3. 警戒怎么表现 → 面向玩家 + 对峙计时；
4. 追击怎么触发、怎么中断 → 对峙超时置 `bShouldChase`；战前丢失视野可清除，正式入战后仅由领地边界、死亡或目标失效中断；
5. 死亡时怎么停 → 死亡状态同步到黑板，行为树整体停掉。

---

## 2. 设计依据与行为边界

### 2.1 对应决策书

| 决策 | 结论 | 本册做法 |
|---|---|---|
| A2 | 内置 AIPerception + BehaviorTree + Blackboard | 视觉感知 + 行为树状态机 |
| D3 | 索敌半径 800～1200 | SightRadius 默认 1000 |
| D4 | 视野左右各 60° | PeripheralVisionAngleDegrees = 60 |
| D5 | 视觉刺激最大记忆年龄 5 秒 | AIPerception MaxAge = 5；正式入战后忽略视觉失败 |
| D26 | 核心用 C++ | 距离服务、对峙任务、死亡同步用 C++；移动用内置 MoveTo |

### 2.2 三条必须记住的行为边界

1. **正式战斗一旦开始，即使玩家跑出警戒范围或绕到 BOSS 身后，BOSS 也会继续追**。只有玩家离开 BOSS 的有效领地、目标死亡/失效或脚本明确重置，才能脱战。
2. **玩家能被看到但不在警戒范围内 → BOSS 继续巡逻**。警戒范围才是触发条件，视野大不等于一定进入战斗。
3. **本册“攻击范围”只做状态与占位**：进入攻击范围后 BOSS 停下面向玩家。真正的攻击、攻击后的对峙循环由下一册实现。

---

## 3. 涉及文件与资源

### 3.1 新增 C++

| 文件（建议路径） | 父类 | 职责 |
|---|---|---|
| `Source/First/Public/AI/Services/UBTService_UpdateCombatDistance.h`<br>`Source/First/Private/AI/Services/UBTService_UpdateCombatDistance.cpp` | `UBTService` | 计算与玩家距离，写 `bInAlertRange` / `bInAttackRange` |
| `Source/First/Public/AI/Services/UBTService_UpdateBossState.h`<br>`Source/First/Private/AI/Services/UBTService_UpdateBossState.cpp` | `UBTService` | 同步死亡状态到黑板；死亡时停止 AI 行为 |
| `Source/First/Public/AI/Tasks/UBTTask_BossAlert.h`<br>`Source/First/Private/AI/Tasks/UBTTask_BossAlert.cpp` | `UBTTaskNode` | 面向玩家 + 对峙计时 + 触发追击 |

### 3.2 修改

| 文件 | 修改内容 |
|---|---|
| `BossAIController.h/.cpp` | 修改 + 新增（感知配置、目标事件、新黑键初值，见 4.0 改动清单） |
| `BB_Boss` | 新增 6 个黑板键 |
| `BT_Boss` | 增加服务与 4 个行为分支 |
| `BP_BossAIController` | 检查感知配置（SightConfig 数值） |
| `BP_Boss` | 检查 AI 控制器类指向 `BP_BossAIController`、主角可被感知 |

### 3.3 本册改动总览（按文件找位置）

| 文件 | 位置 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `BossAIController.h` | `BossBehaviorTree` 属性下方 | 新增 | 感知属性：SightConfig、SightRadius、LoseSightRadius、PeripheralVisionAngleDegrees、LoseSightTime |
| `BossAIController.h` | `OnPossess` 声明上方 | 新增 | `HandleTargetPerceptionUpdated` 回调声明 |
| `BossAIController.cpp` | 构造函数，`BrainComponent` 之后 | 新增 | 视觉感知配置（SightConfig 创建与挂载） |
| `BossAIController.cpp` | `OnPossess` 中 `Super::OnPossess` 之后 | 新增 | 感知事件绑定 |
| `BossAIController.cpp` | `InitializeBossBlackboard` 巡逻初值之后 | 新增 | 6 个索敌黑键初值 |
| `BossAIController.cpp` | 文件末尾 | 新增 | `HandleTargetPerceptionUpdated` 函数完整实现 |
| `UBTService_UpdateCombatDistance.h/.cpp` | 新建文件 | 新增 | 距离计算服务（AttackRadius / AlertRadius → 黑键） |
| `UBTService_UpdateBossState.h/.cpp` | 新建文件 | 新增 | 死亡状态同步服务 |
| `UBTTask_BossAlert.h/.cpp` | 新建文件 | 新增 | 警戒/对峙任务（面向玩家 + 计时） |

---

## 4. 第一步：ABossAIController 增加视觉感知

这两个文件在上一册已经创建。本册是在**上一册版本的基础上修改 + 新增**，下面给出的是改完后的完整文件（方便整体核对）；如果你只想按增量改，先看 4.0 改动清单，按"位置 → 新增内容"逐项加。

### 4.0 相对巡逻手册版本的改动清单

| 位置 | 操作 | 新增/修改内容 |
|---|---|---|
| `BossAIController.h`：`BossBehaviorTree` 属性下方 | 新增 | 感知属性与 `SightConfig` 声明 |
| `BossAIController.h`：`OnPossess` 声明上方 | 新增 | `HandleTargetPerceptionUpdated` 回调声明 |
| `BossAIController.cpp`：构造函数 `BrainComponent` 之后 | 新增 | 先创建感知组件（PerceptionComponent），再挂视觉配置（ConfigureSense、SetDominantSense） |
| `BossAIController.cpp`：`OnPossess` 中 `Super::OnPossess` 之后 | 新增 | 感知事件绑定（OnTargetPerceptionUpdated） |
| `BossAIController.cpp`：`InitializeBossBlackboard` 的巡逻初值之后 | 新增 | 6 个索敌黑键初值（TargetActor、bPlayerDetected、bInAlertRange、bInAttackRange、bShouldChase、bIsDead） |
| `BossAIController.cpp`：文件末尾 | 新增 | `HandleTargetPerceptionUpdated` 函数完整实现 |

### 4.1 头文件

> 【改动位置】文件 `BossAIController.h`（相对巡逻手册版本）
> 【新增内容】见 4.0 清单前两行：感知属性、`HandleTargetPerceptionUpdated` 回调声明。下面是完整新版本。

`Source/First/Public/Controllers/BossAIController.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BossAIController.generated.h"

class ABossCharacter;
class UAISenseConfig_Sight;
class UBehaviorTree;
struct FAIStimulus;

/**
 * BOSS 的 AI 控制器。
 *
 * 职责：
 * 1. 配置视觉感知（UAISenseConfig_Sight）；
 * 2. 感知到/丢失玩家时更新黑板；
 * 3. 初始化黑板初值并启动行为树。
 */
UCLASS()
class FIRST_API ABossAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABossAIController();

	// 在 BP_BossAIController 的类默认值中配置：巡逻/战斗行为树资产。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI")
	TObjectPtr<UBehaviorTree> BossBehaviorTree;

	// 视觉感知配置对象，可在编辑器中直接调数值。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// 索敌半径：玩家进入这个距离且在视野内会被发现（决策书 D3：800～1200）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="100.0"))
	float SightRadius = 1000.f;

	// 走出多远后判定“看不到”（一般比 SightRadius 大一些，防止在边界抖动）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="100.0"))
	float LoseSightRadius = 1400.f;

	// 视野半角：左右各 60°，合计 120°（决策书 D4）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="0.0", ClampMax="180.0"))
	float PeripheralVisionAngleDegrees = 60.f;

	// 视觉刺激的最大记忆年龄。它不是视觉失败回调的延迟时间。
	// 正式入战后视觉失败不再作为脱战条件。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI|Perception", meta=(ClampMin="0.5"))
	float LoseSightTime = 5.f;

	// 快捷访问当前控制的 BOSS。
	UFUNCTION(BlueprintCallable, Category="Boss|AI")
	ABossCharacter* GetBossCharacter() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;

	// 感知事件：看到/丢失玩家时更新黑键。
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	// 初始化黑板初值并启动行为树。
	void InitializeBossBlackboard();
};
```

### 4.2 源文件

> 【改动位置】文件 `BossAIController.cpp`（相对巡逻手册版本）
> 【新增内容】见 4.0 清单后四行：构造函数感知配置、OnPossess 事件绑定、索敌黑键初值、新增回调函数。下面是完整新版本。

`Source/First/Private/Controllers/BossAIController.cpp`：

```cpp
#include "Controllers/BossAIController.h"

#include "AITypes.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Character/BaseCharacter.h"
#include "Character/BossCharacter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"

ABossAIController::ABossAIController()
{
	// 换成行为树专用的黑键组件和脑组件（标准做法）。
	Blackboard = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BossBlackboard"));
	BrainComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BossBrain"));

	// —— 视觉感知配置 ——
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("BossSightConfig"));
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;

	// MaxAge 控制刺激记忆过期，不会把视觉失败回调固定延迟 5 秒。
	// 正式入战后不能据此脱战。
	SightConfig->SetMaxAge(LoseSightTime);

	// 本阶段不区分队伍：玩家和 BOSS 都按“敌方/中立”处理，保证能互相感知。
	// 以后有多阵营（队友、多 BOSS）时再开 GenericTeamId。
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// 【UE 5.6 修正】AIController 不会自动创建感知组件，必须先创建，再挂视觉配置。
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("BossPerception"));
	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
}

ABossCharacter* ABossAIController::GetBossCharacter() const
{
	return Cast<ABossCharacter>(GetPawn());
}

void ABossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 绑定感知事件：看到/丢失目标时回调。
	PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(
		this,
		&ThisClass::HandleTargetPerceptionUpdated);

	InitializeBossBlackboard();
}

void ABossAIController::InitializeBossBlackboard()
{
	ABossCharacter* Boss = GetBossCharacter();
	UBlackboardComponent* BB = GetBlackboardComponent();

	if (!Boss)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少 BOSS 角色（请确认 AI 控制器类挂在 BP_Boss 上）"));
		return;
	}
	if (!BB)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少黑键组件"));
		return;
	}
	if (!BossBehaviorTree)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少行为树资产（请在 BP_BossAIController 类默认值中把 BossBehaviorTree 设为 BT_Boss）"));
		return;
	}

	// 先让黑键组件使用行为树配套的黑板资产，再写键值。
	if (UBlackboardData* BBAsset = BossBehaviorTree->GetBlackboardAsset())
	{
		// 【UE 5.6 修正】UseBlackboard 必须同时传入黑板资产和黑键组件。
		UseBlackboard(BBAsset, BB);
	}

	// 巡逻初值。
	BB->SetValueAsVector(TEXT("HomeLocation"), Boss->HomeLocation);
	BB->SetValueAsVector(TEXT("PatrolLocation"), Boss->GetActorLocation());

	// 索敌初值。
	BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
	BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
	BB->SetValueAsBool(TEXT("bInAlertRange"), false);
	BB->SetValueAsBool(TEXT("bInAttackRange"), false);
	BB->SetValueAsBool(TEXT("bShouldChase"), false);
	BB->SetValueAsBool(TEXT("bIsDead"), false);

	RunBehaviorTree(BossBehaviorTree);
}

void ABossAIController::HandleTargetPerceptionUpdated(
	AActor* Actor,
	FAIStimulus Stimulus)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB || !Actor)
	{
		return;
	}

	// 只把“带 GAS 的角色”当目标，排除自己、武器、特效等无关 Actor。
	ABaseCharacter* Candidate = Cast<ABaseCharacter>(Actor);
	if (!Candidate || Candidate == GetPawn())
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		// 发现玩家：写入目标，并让 AIController 的 Focus 对准它。
		BB->SetValueAsObject(TEXT("TargetActor"), Candidate);
		BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
		SetFocus(Candidate, EAIFocusPriority::Gameplay);
	}
	else
	{
		// 目标从感知中消失（MaxAge 到期）：只有清掉的是当前目标时才重置状态。
		AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
		if (CurrentTarget == Candidate)
		{
			// 教学阶段可用 bShouldChase 表示战斗已经提交。
			// 当前项目的完整实现还会同时检查 DrawSword、WeaponDrawn 和 NavMesh。
			if (BB->GetValueAsBool(TEXT("bShouldChase")))
			{
				BB->SetValueAsBool(TEXT("bPlayerDetected"), true);
				return;
			}

			// 只有尚未正式入战的警戒目标，才因视觉丢失回巡逻。
			BB->SetValueAsObject(TEXT("TargetActor"), nullptr);
			BB->SetValueAsBool(TEXT("bPlayerDetected"), false);
			BB->SetValueAsBool(TEXT("bInAlertRange"), false);
			BB->SetValueAsBool(TEXT("bInAttackRange"), false);
			BB->SetValueAsBool(TEXT("bShouldChase"), false);
			ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
}
```

讲解：

- `SightConfig->SetMaxAge(5)` 控制成功刺激的记忆过期；`WasSuccessfullySensed() == false` 在刚判定看不见时就可能到达，不能把 `MaxAge` 当作脱战倒计时；
- 感知回调里**只负责目标的有无**，不做距离判断；距离判断交给服务（下一步），职责分开；
- `WasSuccessfullySensed() == true` 表示这次感知成功（看到）；`false` 表示目标丢失；
- `SetFocus` 让 AI 的“视线”指向玩家，配合对峙任务里的显式旋转表现更稳定；
- 目标过滤规则目前是“任何带 GAS 的 `ABaseCharacter`”。测试场景里如果放了多个敌人，BOSS 可能把它们也当目标；以后用队伍 Tag/TeamId 区分（见第 13 节）。

---

## 5. 第二步：UBTService_UpdateCombatDistance

### 5.1 用编辑器向导创建类

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索父类 `BTService`（即 `UBTService`）；
3. 类名填写 `BTService_UpdateCombatDistance`（**不要带 U 前缀**：向导会自动加 U，最终类名就是 `UBTService_UpdateCombatDistance`；如果自己带 U 填成 `UBTService_UpdateCombatDistance`，最终类名会变成 `UUBTService_UpdateCombatDistance`，导致类名和构造函数名不一致、编译报 C4430）；
4. 文件放到 `Source/First/Public/AI/Services` 与 `Source/First/Private/AI/Services`，把下面的完整内容整体替换进去（**新建文件，内容全部为新增**）。

### 5.2 头文件

> 【改动位置】新建文件 `UBTService_UpdateCombatDistance.h`
> 【新增内容】`AttackRadius`、`AlertRadius` 两个距离参数；`TickNode()` 声明

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_UpdateCombatDistance.generated.h"

/**
 * 每帧（按 Interval 节流）计算 BOSS 与当前目标的距离，
 * 并把结果写入黑板：
 *   bInAlertRange  = 距离 <= AlertRadius
 *   bInAttackRange = 距离 <= AttackRadius
 *
 * 只做“距离 → 黑键”，不决定任何行为；行为由行为树分支决定。
 */
UCLASS()
class FIRST_API UBTService_UpdateCombatDistance : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCombatDistance();

	// 攻击范围：玩家进入后 BOSS 进入攻击分支（本册占位）。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float AttackRadius = 250.f;

	// 警戒范围：玩家进入后 BOSS 面向玩家并开始对峙计时。
	UPROPERTY(EditAnywhere, Category="Boss|CombatRange", meta=(ClampMin="0.0"))
	float AlertRadius = 800.f;

protected:
	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
```

### 5.3 源文件

> 【改动位置】新建文件 `UBTService_UpdateCombatDistance.cpp`
> 【新增内容】`TickNode()` 完整实现：距离计算 → 写 `bInAlertRange` / `bInAttackRange`

```cpp
#include "AI/Services/UBTService_UpdateCombatDistance.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTService_UpdateCombatDistance::UBTService_UpdateCombatDistance()
{
	NodeName = TEXT("Update Combat Distance");
}

void UBTService_UpdateCombatDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController)
	{
		return;
	}

	const APawn* BossPawn = AIController->GetPawn();
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));

	if (!BossPawn || !Target)
	{
		// 没有目标时清掉范围标志，确保行为树能回巡逻分支。
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		return;
	}

	const float Distance = FVector::Dist(
		BossPawn->GetActorLocation(),
		Target->GetActorLocation());

	BB->SetValueAsBool(TEXT("bInAlertRange"), Distance <= AlertRadius);
	BB->SetValueAsBool(TEXT("bInAttackRange"), Distance <= AttackRadius);
}
```

讲解：

- 服务挂在行为树**根节点**上，无论当前执行哪个分支都会持续更新距离；
- 在编辑器中把服务的**间隔（Interval）**设为 0.2 秒，避免每帧都算（学习阶段也够用）；
- `AttackRadius` / `AlertRadius` 是任务的 `EditAnywhere` 属性，在行为树节点详情里直接调；
- 两个范围是“包含关系”：进入攻击范围时 `bInAlertRange` 也一定为 true，这不影响分支选择，因为攻击分支优先级更高。

---

### 5.4 追击有效性检查：目标不在 NavMesh 上就放弃追击（2026-08 新增）

> 【解决的问题】玩家跳上不可达位置 / 跑出 NavMesh 时，Move To 会部分寻路到最近可达点，
> BOSS 原地停住、行为树一直卡在追击分支。

在 `UBTService_UpdateCombatDistance::TickNode()` 里，距离计算**之前**加一个判断：
目标的位置能否投影到 NavMesh 上。不能 → 把警戒/攻击/追击标志全部复位，行为树自动回巡逻。

`UBTService_UpdateCombatDistance.cpp` 完整新版本：

```cpp
#include "AI/Services/UBTService_UpdateCombatDistance.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"

UBTService_UpdateCombatDistance::UBTService_UpdateCombatDistance()
{
	NodeName = TEXT("Update Combat Distance");
}

void UBTService_UpdateCombatDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController)
	{
		return;
	}

	const APawn* BossPawn = AIController->GetPawn();
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));

	if (!BossPawn || !Target)
	{
		// 没有目标时清掉范围标志，确保行为树能回巡逻分支。
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		return;
	}

	// 【2026-08 修复】目标不在 NavMesh 上（玩家跳上不可达位置）→ 放弃警戒/追击，回巡逻。
	// 不主动清 TargetActor：玩家回到 NavMesh 后，下一轮距离计算会自动重新触发警戒。
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(BossPawn->GetWorld());
	FNavLocation ProjectedLocation;
	const bool bTargetOnNavMesh = NavSys && NavSys->ProjectPointToNavigation(
		Target->GetActorLocation(),
		ProjectedLocation,
		FVector(100.f, 100.f, 100.f));

	if (!bTargetOnNavMesh)
	{
		BB->SetValueAsBool(TEXT("bInAlertRange"), false);
		BB->SetValueAsBool(TEXT("bInAttackRange"), false);
		// 必须同时取消追击，否则追击分支还会一直尝试 MoveTo。
		BB->SetValueAsBool(TEXT("bShouldChase"), false);
		// 清除警戒时 SetFocus 留下的焦点，避免回巡逻后还一直面朝玩家。
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	const float Distance = FVector::Dist(
		BossPawn->GetActorLocation(),
		Target->GetActorLocation());

	BB->SetValueAsBool(TEXT("bInAlertRange"), Distance <= AlertRadius);
	BB->SetValueAsBool(TEXT("bInAttackRange"), Distance <= AttackRadius);
}
```

讲解：

- `ProjectPointToNavigation`：把目标位置往 NavMesh 上投影，投影成功说明目标在（或贴近）可走区域，失败说明目标在网格外；
- 第三个参数 `FVector(100,100,100)` 是投影容差。玩家站在网格边缘 100 以内仍算"可到达"；想更严格就调小；
- **只复位标志、不清 TargetActor**：玩家回到 NavMesh 后，服务下一轮算出 `bInAlertRange = true`，警戒分支（Abort = Both）会再次打断巡逻，BOSS 自然重新进入警戒 → 追击，不需要感知系统重新"发现"；
- **`ClearFocus` 不能省**：警戒任务 `UBTTask_BossAlert` 在开始时调用了 `SetFocus`（并每帧旋转面向玩家）。放弃追击时若不清除焦点，BOSS 回到巡逻后仍会保留面向玩家的朝向（若 BP_Boss 误开了 `bUseControllerRotationYaw`，控制器还会持续把角色往玩家方向转）。清除焦点后，巡逻时角色由 `bOrientRotationToMovement` 决定朝向——移动时面向移动方向，停下时保持到达方向；
- 编译后做一次普通 Build。

> 局限：这个检查只判断"目标在不在 NavMesh 上"。如果玩家站在网格内、但和 BOSS 隔着不可翻越的障碍，投影仍会成功。
> 以后想处理这种情况，可以改用"路径是否存在"查询（`GetPathLength` 或 `DoesPathExist`），本阶段先不做。

---

## 6. 第三步：UBTService_UpdateBossState（死亡同步）

### 6.1 创建类

父类 `UBTService`，类名填写 `BTService_UpdateBossState`（**不要带 U 前缀**，向导会自动加 U，最终类名 `UBTService_UpdateBossState`），目录同上。

### 6.2 头文件

> 【改动位置】新建文件 `UBTService_UpdateBossState.h`
> 【新增内容】`TickNode()` 声明

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "UBTService_UpdateBossState.generated.h"

/**
 * 把 BOSS 的 GAS 状态同步到黑板：
 *   bIsDead = ASC 上是否存在 Shared.Status.Dead。
 *
 * BOSS 死亡时：停止移动、清除目标，让行为树落到待机分支。
 */
UCLASS()
class FIRST_API UBTService_UpdateBossState : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateBossState();

protected:
	virtual void TickNode(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
```

### 6.3 源文件

> 【改动位置】新建文件 `UBTService_UpdateBossState.cpp`
> 【新增内容】`TickNode()` 完整实现：读取 `Shared.Status.Dead` → 写 `bIsDead`；死亡时停移动、清目标

```cpp
#include "AI/Services/UBTService_UpdateBossState.h"

#include "AIController.h"
#include "AITypes.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BaseCharacter.h"
#include "MyGameplayTags.h"

UBTService_UpdateBossState::UBTService_UpdateBossState()
{
	NodeName = TEXT("Update Boss State");
}

void UBTService_UpdateBossState::TickNode(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!BB || !AIController)
	{
		return;
	}

	ABaseCharacter* Boss = Cast<ABaseCharacter>(AIController->GetPawn());
	if (!Boss || !Boss->GetFirstAbilitySystemComponent())
	{
		return;
	}

	const bool bIsDead = Boss->GetFirstAbilitySystemComponent()
		->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead);

	BB->SetValueAsBool(TEXT("bIsDead"), bIsDead);

	if (bIsDead)
	{
		// 死亡：立即停移动、解除注视。
		// ⚠️ 不要复位 bInAlertRange / bInAttackRange / bShouldChase / TargetActor——
		// 这些键正是警戒/巡逻分支装饰器的观察键，复位会触发分支重新评估，
		// 与根节点 bIsDead 装饰器的中止（Abort=Both）互相竞争，导致树停不下来。
		// 树由根装饰器统一停住即可。
		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
```

讲解：

- `Shared.Status.Dead` 是你现有 `FirstAttributeSet` 在生命归零时添加的标签，BOSS 的 ASC 继承自 `ABaseCharacter`，直接可用；
- 死亡状态写进黑板后，行为树根节点的装饰器会阻止所有战斗分支（见 9.2）；
- 这个服务以后还可以继续扩展：写 `bIsStaggered`（大僵直）等状态，行为树据此选择受击分支。

---

## 7. 第四步：UBTTask_BossAlert（警戒/对峙）

### 7.1 创建类

父类 `UBTTaskNode`，类名填写 `BTTask_BossAlert`（**不要带 U 前缀**，向导会自动加 U，最终类名 `UBTTask_BossAlert`），目录 `AI/Tasks`。

### 7.2 头文件

> 【改动位置】新建文件 `UBTTask_BossAlert.h`
> 【新增内容】`StareDuration`、`bRotateToFaceTarget` 参数；`ExecuteTask()`、`TickTask()` 声明

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "UBTTask_BossAlert.generated.h"

/**
 * 警戒/对峙任务。
 *
 * 行为：
 * 1. 面向玩家（每帧旋转到玩家方向）；
 * 2. 保持对峙 StareDuration 秒；
 * 3. 若对峙期间玩家离开警戒范围或目标丢失 → 任务失败（回巡逻）；
 * 4. 对峙完成 → 把 bShouldChase 置 true（触发追击）。
 */
UCLASS()
class FIRST_API UBTTask_BossAlert : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BossAlert();

	// 对峙时长：进入警戒范围后看玩家多久才发起追击。
	UPROPERTY(EditAnywhere, Category="Boss|Alert", meta=(ClampMin="0.0"))
	float StareDuration = 3.f;

	// 警戒期间是否持续旋转面向玩家（关闭则只对峙不转头）。
	UPROPERTY(EditAnywhere, Category="Boss|Alert")
	bool bRotateToFaceTarget = true;

protected:
	virtual uint16 GetInstanceMemorySize() const override;
	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds) override;
};
```

### 7.3 源文件

> 【改动位置】新建文件 `UBTTask_BossAlert.cpp`
> 【新增内容】`FBossAlertTaskMemory` 结构；`ExecuteTask()` / `TickTask()` 完整实现（面向玩家、对峙计时、置 `bShouldChase`）

```cpp
#include "AI/Tasks/UBTTask_BossAlert.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

// 每个任务实例自己持有的临时数据，放在任务内存里，而不是类的成员。
struct FBossAlertTaskMemory
{
	float ElapsedTime = 0.f;
};

UBTTask_BossAlert::UBTTask_BossAlert()
{
	NodeName = TEXT("Boss Alert");

	// 【UE 5.6 修正】任务节点默认不调用 TickTask；
	// 必须用 INIT_TASK_NODE_NOTIFY_FLAGS() 打开通知开关，
	// 否则对峙任务会一直停在 InProgress，行为树卡在警戒分支。
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

uint16 UBTTask_BossAlert::GetInstanceMemorySize() const
{
	return sizeof(FBossAlertTaskMemory);
}

EBTNodeResult::Type UBTTask_BossAlert::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* Target = BB
		? Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")))
		: nullptr;

	if (!AIController || !BB || !Target)
	{
		return EBTNodeResult::Failed;
	}

	FBossAlertTaskMemory* Memory =
		reinterpret_cast<FBossAlertTaskMemory*>(NodeMemory);
	Memory->ElapsedTime = 0.f;

	// 让 AIController 的 Focus 指向玩家（为以后追击中的转向打基础）。
	AIController->SetFocus(Target, EAIFocusPriority::Gameplay);

	// 对峙需要时间，返回 InProgress，由 TickTask 决定何时结束。
	return EBTNodeResult::InProgress;
}

void UBTTask_BossAlert::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();

	if (!AIController || !BB)
	{
		// 【UE 5.6 修正】FinishLatentTask 只需要两个参数，不再传 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));

	// 对峙中断条件：目标丢失，或玩家离开了警戒范围。
	if (!Target || !BB->GetValueAsBool(TEXT("bInAlertRange")))
	{
		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	FBossAlertTaskMemory* Memory =
		reinterpret_cast<FBossAlertTaskMemory*>(NodeMemory);
	Memory->ElapsedTime += DeltaSeconds;

	// 面向玩家：只旋转 Yaw，保持站立姿态。
	// 注意：ACharacter 默认由移动方向决定朝向（bOrientRotationToMovement），
	// 站立不动时不会自动转向玩家，所以这里显式旋转。
	if (bRotateToFaceTarget)
	{
		if (APawn* Pawn = AIController->GetPawn())
		{
			const FVector ToTarget =
				Target->GetActorLocation() - Pawn->GetActorLocation();
			const FRotator TargetRotation = ToTarget.Rotation();
			Pawn->SetActorRotation(FRotator(0.f, TargetRotation.Yaw, 0.f));
		}
	}

	if (Memory->ElapsedTime >= StareDuration)
	{
		// 对峙结束：允许追击，并触发 BOSS 拔剑（CombatStart）。
		BB->SetValueAsBool(TEXT("bShouldChase"), true);

		// 拔剑时机：对峙结束才发，而不是"看见就拔"。
		// 需要 include：AbilitySystemBlueprintLibrary.h / Abilities/GameplayAbilityTypes.h
		//           Character/BossCharacter.h / MyGameplayTags.h
		if (ABossCharacter* Boss = Cast<ABossCharacter>(AIController->GetPawn()))
		{
			FGameplayEventData EventData;
			EventData.Instigator = Target;
			EventData.Target = Target;
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Boss,
				MyGameplayTags::Boss_Event_CombatStart,
				EventData);
		}

		// 【UE 5.6 修正】两参数版本，不要带 NodeMemory。
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}
```

讲解：

- 对峙是有时长的任务，所以走 `ExecuteTask → InProgress → TickTask → FinishLatentTask` 这条异步路径。**构造函数里必须先调用 `INIT_TASK_NODE_NOTIFY_FLAGS()` 打开 Tick 通知**，否则 `TickTask` 永远不会被调用，任务会一直卡在 `InProgress`（具体见文末 14.3）。注意 UE 5.6 的 `FinishLatentTask` 签名是 `(OwnerComp, TaskResult)` 两个参数，不再传 `NodeMemory`；
- `GetInstanceMemorySize` 让每个任务实例有独立的计时数据，避免多个 BOSS 共享成员变量（行为树节点默认是共享的，这点最容易写错）；
- 中断条件（目标丢失/离开警戒范围）直接返回 `Failed`，行为树自动退回巡逻分支；
- 对峙结束后置 `bShouldChase = true`，警戒分支的装饰器失效，行为树自动切到追击分支；
- 为什么显式 `SetActorRotation`：`SetFocus` 只影响 AIController 的朝向，而 `ACharacter` 站着不动时不会跟随旋转；直接在 Tick 里转角色是最直观的做法。以后想做平滑转头，可参考 **Warrior 完整版**（`D:\UE2026\ue5-warrior-main`）的 `BTTask_RotateToFaceTarget`：用 `FMath::RInterpTo` 插值旋转 + `AnglePrecision` 判定到位角度，转角更自然；`BTService_OrientToTargetActor` 则是"每帧持续平滑面向目标"的服务版。

---

## 8. 第五步：编译

按顺序编译，每个闸门一次普通 Build：

```text
闸门 1：ABossAIController（感知 + 事件）
闸门 2：UBTService_UpdateCombatDistance
闸门 3：UBTService_UpdateBossState
闸门 4：UBTTask_BossAlert
```

常见编译错误：

| 报错 | 原因 | 修复 |
|---|---|---|
| 找不到 `FAIStimulus` | 缺少感知相关 include | 检查 `AIPerceptionComponent.h`、`AIPerceptionTypes.h` |
| `HandleTargetPerceptionUpdated` 无法绑定 | 回调没有 `UFUNCTION()` | 确认头文件里函数带 `UFUNCTION()` 标记 |
| 找不到 `GetFirstAbilitySystemComponent` | 没 include BaseCharacter | 检查 `Character/BaseCharacter.h` |
| `UseBlackboard` 不接受 1 个参数 | UE 5.6 需要同时传黑板资产与黑键组件 | 改为 `UseBlackboard(BBAsset, BB)` |
| `FinishLatentTask` 参数不匹配 | UE 5.6 签名是 `(OwnerComp, TaskResult)` | 去掉中间那个 `NodeMemory` 参数 |
| 编译报 C4430 / C4183：`UBTService_...` 缺少返回类型 | 新建类时名字带了 U 前缀，向导生成 `UUBTService_...`，构造函数却写 `UBTService_...`，类名和构造函数名不一致 | 统一类名（去掉多余前缀）；以后新建类时名字**不要带 U/B/A 前缀**，让向导自己加 |

---

## 9. 第六步：编辑器资产配置

### 9.1 BB_Boss 新增黑板键

在上一册已有的 `HomeLocation`、`PatrolLocation` 基础上，新增：

| 键名 | 类型 | 说明 |
|---|---|---|
| `TargetActor` | Object | 当前锁定目标（玩家） |
| `bPlayerDetected` | Bool | 是否存在有效目标 |
| `bInAlertRange` | Bool | 玩家在警戒范围内（且不在攻击范围时触发警戒） |
| `bInAttackRange` | Bool | 玩家在攻击范围内 |
| `bShouldChase` | Bool | 是否允许追击（对峙完成后置 true） |
| `bIsDead` | Bool | BOSS 是否死亡 |
| `bIsBusy` | Bool | 忙碌中（攻击/拔剑等；由 Update Boss State 从 `ActionBlockingTags` 聚合） |

键名必须与 C++ 中的字符串一致。

### 9.2 BT_Boss 完整结构

在上一册的巡逻分支基础上调整，最终结构（从上到下优先级从高到低）：

```text
BT_Boss（根 = 选择器 Selector）
├─ 根节点服务（Service）：Update Combat Distance（间隔 Interval = 0.2）
│                         Update Boss State（间隔 Interval = 0.2）
│
├─ 死亡分支（最高优先级）★
│   装饰器：bIsDead == true（Abort = 两者 Both）
│   └─ 等待（Wait）≈ 99999 秒（死亡待机占位；以后可换死亡表现任务）
│
├─ 战斗分支（选择器 Selector）★ 按情境分层：进入战斗后的所有逻辑都收在这棵子树
│   装饰器：bShouldChase == true（Abort = 两者 Both）
│   ├─ 攻击分支（序列 Sequence）
│   │   装饰器：bInAttackRange == true（Abort = 低优先级 Lower Priority）
│   │   └─ Boss Alert（占位：面向玩家；下一册替换为 BTTask_BossAttack）
│   └─ 追击分支（序列 Sequence）
│       装饰器：bInAttackRange == false && bIsBusy == false（Abort = 两者 Both）
│       └─ 移至（Move To）
│            黑板键（Blackboard Key）= TargetActor
│            可接受半径（Acceptable Radius）= 180
│
├─ 警戒分支（序列 Sequence）
│   装饰器：bInAlertRange == true && bShouldChase == false（Abort = 两者 Both）
│   └─ Boss Alert（对峙时间 Stare Duration = 3.0）
│
├─ 巡逻分支（序列 Sequence）
│   装饰器：bInAlertRange == false && bShouldChase == false && bIsBusy == false
│   ├─ Find Patrol Location
│   ├─ 移至（Move To）（PatrolLocation，可接受半径 Acceptable Radius = 60）
│   └─ 等待（Wait）（巡逻停留 5.0，见《BOSS待机与移动动画装配实现指导》6.3）
│
└─ 待机（可选兜底：什么都不做，防止所有分支都失败时空转报错）
```

配置要点：

1. **根节点服务**：右键根节点 → **添加服务（Add Service）**，选 `Update Combat Distance` 和 `Update Boss State`，把**间隔（Interval）**设为 0.2；
2. **装饰器**：右键分支节点 → **添加装饰器（Add Decorator）** → **黑板（Blackboard）**，配置键和条件；**战斗、攻击、追击、警戒分支的装饰器都要设置中止（Abort）**：
   - 战斗分支：`两者（Both）`（对峙结束置 `bShouldChase=true` 时立刻从警戒切到战斗子树）；
   - 攻击分支：`低优先级（Lower Priority）`（玩家进攻击范围时打断追击/靠近）；
   - 追击分支：`两者（Both）`（距离进入攻击范围时立刻切换）；
   - **警戒分支：`两者（Both）`**（⚠️ 最容易漏！不设的话，玩家进入警戒范围时，正在执行的巡逻分支**不会被中断**，BOSS 会继续走完当前巡逻点、等完停留再进入警戒）。
   否则条件变化时不能中断正在运行的其它分支。
   注意：攻击、追击都在"战斗"子树**里面**，它们的装饰器只写距离键（`bInAttackRange`）就够了，**不用再重复写 `bShouldChase`**——战斗分支已经保证了这一点；
3. **追击分支的移至（Move To）目标用对象键 `TargetActor`**，不是向量键；
4. **巡逻分支装饰器必须是“不在警戒范围且不追击且不攻击中”**，否则玩家出了警戒范围后，巡逻分支会抢走追击分支（这是最容易出的 bug）；
5. **攻击分支本册用 Boss Alert 占位**：验收时看到“玩家进攻击范围 → BOSS 停下面向玩家”即可；真正的攻击与攻击后对峙循环在下一册替换这一分支。
6. **忙碌状态挡追击/巡逻（防"边攻击边位移"和"拔剑滑步"）**：攻击/拔剑能力激活时分别挂
   `Boss.Status.Attacking` / `Boss.Status.DrawSword`；`Update Boss State` 服务用可配置的
   `ActionBlockingTags` 容器聚合出**单一黑板键 `bIsBusy`**，追击/巡逻装饰器只要求 `bIsBusy == false`。
   以后新增僵直等状态，只需在服务节点的 `ActionBlockingTags` 里加标签，行为树不用再加键和装饰器。
7. **死亡拦截用"最高优先级死亡分支"，不要用根装饰器，也不用每个分支都加**：
   UE 的根级装饰器只是树的**入口条件**，运行期间不会保持黑键观察，中止在主树上不可靠。
   正确做法：在 Selector **最顶端**加一个"死亡分支"，装饰器 `bIsDead == true`（Abort = Both），
   子节点放一个不会结束的任务（如 Wait 99999）。死亡时该分支打断所有低优先级分支并常驻，
   其它行为全部停止；以后新增技能/行为只要放在它下面即可，无需额外装饰器。
8. **"战斗分支"是以后所有战斗行为的容器**：三距离对峙（《BOSS三距离对峙与受击窗口实现指导》）里的
   后退 / 侧移 / 靠近分支都会加进这棵子树，和攻击、追击并列，装饰器只写距离键、不用重复写 `bShouldChase`；
   `bIsBusy` 判断放在"会移动的分支"上（追击，以及以后的后退/侧移），攻击分支不要求，保证攻击动作本身不受影响。

### 9.3 检查 BP_Boss 与地图

1. 感知数值默认来自 C++ 的 `SightConfig`，也可以在 `BP_BossAIController` 的类默认值里覆盖；
2. 确认主角可以被感知：主角网格体（Mesh）/胶囊体（Capsule）的碰撞响应包含 Visibility/Pawn 通道（默认就有，若做过隐身/关碰撞要恢复）；
3. 确认地图里有覆盖到战斗区域的导航网格（NavMesh，按 P 显示绿色网格）；
4. 若地图里还有二阶段用的测试敌人，先移走或放到警戒范围外，避免 BOSS 把别的敌人当目标。

---

## 10. 运行时完整流程

```text
玩家进入警戒范围（距离 <= 800）
→ Update Combat Distance：bInAlertRange = true
→ 警戒分支装饰器（Abort = Both）立刻打断正在执行的巡逻分支
→ 警戒分支激活：Boss Alert
   → BOSS 停下、每帧转向玩家、对峙计时
→ 对峙满 3 秒：bShouldChase = true
→ 追击分支激活：移至（Move To）（TargetActor，可接受半径 Acceptable Radius = 180）
→ 玩家进入攻击范围（距离 <= 250）
→ Update Combat Distance：bInAttackRange = true
→ 攻击分支激活（本册占位：面向玩家）
→ 玩家跑出警戒范围：追击不中断（bShouldChase 仍为 true）
→ 玩家绕到背后或躲到看不见的地方超过 5 秒：保持 TargetActor，继续战斗
→ 玩家离开 BOSS 的有效 NavMesh 领地：关闭战斗注视与追击，回巡逻
```

---

## 11. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 巡逻中不理会玩家 | 玩家站在警戒范围（800）外 | BOSS 继续巡逻 |
| 警戒触发 | 玩家走进警戒范围 | BOSS 停下并转向玩家 |
| 对峙后追击 | 对峙 3 秒不动 | BOSS 开始追击玩家 |
| 出圈继续追 | 对峙后玩家跑出警戒范围 | 追击不中断 |
| 进入攻击范围 | 玩家接近到 250 内 | BOSS 停下面向玩家（占位） |
| 战前丢失视野 | 尚未完成警戒时离开视觉范围或绕到遮挡后 | 感知失败后 BOSS 放弃警戒，回到巡逻 |
| 战斗中绕背 | 正式入战后一直站在 BOSS 身后 | BOSS 保留目标并继续转向、追击或攻击 |
| 离开领地脱战 | 正式入战后走出 BOSS 的有效 NavMesh | BOSS 清 Focus、停止追击并回巡逻 |
| 视野限制 | 从 BOSS 正背后接近 | 警戒范围外不会被发现（120° 视野） |
| 死亡停止 | 打死 BOSS | BOSS 停止移动/追击，不再响应 |
| 多次运行 | 反复 Start/Stop PIE | 状态复位正确，不会卡在追击/警戒 |

---

## 12. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| BOSS 永远发现不了玩家 | 感知数值、玩家碰撞或 Affiliation 配置问题 | 检查 SightRadius、玩家胶囊体 Visibility 通道、DetectionByAffiliation |
| 发现玩家但不警戒 | 距离服务没挂在根节点 | 检查 Update Combat Distance 是否挂载、Interval 是否有效 |
| 玩家进入警戒范围，BOSS 不马上停下，还要走完当前巡逻点/停留才警戒 | 警戒分支装饰器没设**中止（Abort）**，巡逻分支没有被立即打断 | 警戒分支的两个黑板装饰器都设 **Abort = 两者（Both）**（见 9.2 配置要点 2） |
| 对峙后不追击 | bShouldChase 没置位或追击装饰器条件错 | 检查 Boss Alert 是否跑满 StareDuration；追击分支装饰器是否匹配 |
| 追击中原地不动、一直停在追击状态 | ① 玩家站在没有 NavMesh 的地方，Move To 部分寻路走到最近可达点后原地停住；② BOSS 被碰撞/障碍卡住，Move To 一直进行中；③ 玩家进入攻击范围时，高亮的其实是**攻击分支**（占位停下，属预期） | ① 按 **5.4** 给距离服务加 NavMesh 有效性检查（首选修复）；② 看 BOSS 是否贴着墙/障碍，检查胶囊体与可通行宽度；③ 用行为树调试器区分：高亮 Move To=寻路问题，高亮攻击分支=占位预期 |
| 追击被打断回巡逻 | 巡逻分支装饰器条件太宽 | 巡逻分支必须是 `bInAlertRange==false && bShouldChase==false` |
| 进入攻击范围没反应 | 攻击分支还是占位 | 下一册实现攻击后替换 |
| 行为树不执行 | BT 没关联黑板资产 | 根节点的黑板资产（Blackboard Asset）必须选 `BB_Boss` |
| 死亡后还在动 | 状态服务没挂或装饰器没配 | 检查 Update Boss State 与 bIsDead 装饰器（Abort = Both） |
| 死后仍在警戒/巡逻循环 | 根级 `bIsDead` 装饰器是**入口条件**，运行期间不保持黑键观察，中止不生效（UE 特性） | 用 **Selector 最顶端的"死亡分支"**（`bIsDead == true`，Abort = Both，子节点 Wait 99999），见 9.2 结构；最后手段才是给每个分支加 `bIsDead == false` |
| BOSS 把其他敌人当目标 | 目标过滤规则太宽 | 本阶段先把测试敌人移开；后续用队伍 Tag 区分 |
| 启动崩溃：EXCEPTION_ACCESS_VIOLATION（读地址 0x...f0），堆栈在 `ABossAIController` 构造函数 | UE 5.6 的 AIController **不会自动创建感知组件**，构造函数里直接调用 `PerceptionComponent->ConfigureSense()` 时它是空指针 | 在构造函数里、调用 ConfigureSense **之前**先创建：`PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("BossPerception"));` |

---

## 13. 下一步与扩展

1. **轻攻击/重攻击 + 攻击后对峙循环**：把攻击分支的占位替换成 `BTTask_BossAttack`（选择 GA、播放蒙太奇、结算伤害）+ 攻击后的对峙等待，形成“攻击 → 对峙 → 再攻击”的循环，见下一册实现指导；
2. **最后已知位置**：目标丢失但未超时时，追击到 `LastKnownPlayerLocation` 而不是追着演员实时位置；
3. **队伍区分**：用 `GenericTeamId` 或 GameplayTag 区分玩家/BOSS/其他敌人，避免误锁定。参考 **Warrior 完整版** `AWAIController`：构造函数 `SetGenericTeamId(FGenericTeamId(1))`，重写 `GetTeamAttitudeTowards` 判断敌我，感知配置里 `bDetectFriendlies = false` 只感知敌方；
4. **听觉感知**：加 `UAISenseConfig_Hearing`，受伤或脚步声触发搜索；
5. **警戒表现**：警戒/追击状态写入 GAS Tag（如 `Boss.Status.Alerting`），驱动 BOSS 动画蓝图播放对应姿势；
6. **平滑转头**：警戒/攻击中持续面向玩家，可参考 Warrior 完整版的 `BTTask_RotateToFaceTarget`（任务版，转到位后成功）或 `BTService_OrientToTargetActor`（服务版，每帧插值）；
7. **防碰撞卡死**：BOSS 追击被障碍卡住时，可参考 Warrior 完整版用 `UCrowdFollowingComponent` 替代默认 `PathFollowingComponent`，开启 Detour Crowd Avoidance 人群避让（避让质量、碰撞查询范围都可配）。

> Warrior 完整版位于 `D:\UE2026\ue5-warrior-main`（完整 GAS 动作框架：敌人 AI、旋转任务、锁定、投射物、UI、存档等），以上条目只摘取与本手册相关的最小参考，需要时可打开对应文件对照学习。

---

## 14. UE 5.6 修正对照（初学者按这个找位置）

以下三处是 UE 5.6 与旧版引擎不同的 API 或容易遗漏的写法。本文里的代码已经修正；如果你的文件是按旧版本抄的，按下面的表逐字改。

### 14.1 修正 1：UseBlackboard（BossAIController.cpp）

和巡逻手册第 14.1 节完全一样：

- 文件：`Source/First/Private/Controllers/BossAIController.cpp`
- 函数：`InitializeBossBlackboard()`
- 搜索关键词：`UseBlackboard`

旧写法（编译报错：不接受 1 个参数）：

```cpp
UseBlackboard(BBAsset);
```

新写法：

```cpp
// 【UE 5.6 修正】UseBlackboard 必须同时传入黑板资产和黑键组件。
UseBlackboard(BBAsset, BB);
```

### 14.2 修正 2：FinishLatentTask（UBTTask_BossAlert.cpp）

- 文件：`Source/First/Private/AI/Tasks/UBTTask_BossAlert.cpp`
- 位置：`TickTask()` 函数里，一共 **3 处**调用
- 搜索关键词：`FinishLatentTask`

旧写法（编译报错：参数不匹配）：

```cpp
FinishLatentTask(OwnerComp, NodeMemory, EBTNodeResult::Failed);
FinishLatentTask(OwnerComp, NodeMemory, EBTNodeResult::Succeeded);
```

新写法（UE 5.6 不再传 `NodeMemory`，每处都改成两个参数）：

```cpp
// 【UE 5.6 修正】FinishLatentTask 只需要两个参数，不再传 NodeMemory。
FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
```

> 提示：旧写法里 `Failed` 出现两次、`Succeeded` 出现一次，共三处，三处都要改。改完后做一次普通 Build。

### 14.3 修正 3：缺少 INIT_TASK_NODE_NOTIFY_FLAGS（UBTTask_BossAlert.cpp 构造函数）

- 文件：`Source/First/Private/AI/Tasks/UBTTask_BossAlert.cpp`
- 位置：构造函数 `UBTTask_BossAlert::UBTTask_BossAlert()`
- 搜索关键词：`NodeName = TEXT("Boss Alert")`

现象（编译不报错，但行为树卡死）：

```text
BOSS 进入警戒后一直对峙，永远不追击；
玩家跑出警戒范围也没用，行为树始终停在 Boss Alert 任务。
```

原因：UE 的 `UBTTaskNode` 默认 `bNotifyTick = false`，`TickTask()` 不会被调用；
`ExecuteTask` 返回 `InProgress` 后没人负责计时和结束任务，任务永远处于"进行中"，
`bShouldChase` 也永远不会被置为 true。

旧写法（缺少开关，TickTask 永远不会执行）：

```cpp
UBTTask_BossAlert::UBTTask_BossAlert()
{
	NodeName = TEXT("Boss Alert");
}
```

新写法：

```cpp
UBTTask_BossAlert::UBTTask_BossAlert()
{
	NodeName = TEXT("Boss Alert");

	// 【UE 5.6 修正】必须打开任务节点的 Tick 通知，TickTask 才会被调用。
	INIT_TASK_NODE_NOTIFY_FLAGS();
}
```

改完后做一次普通 Build，再进 PIE 验证"对峙 3 秒 → 追击"。
