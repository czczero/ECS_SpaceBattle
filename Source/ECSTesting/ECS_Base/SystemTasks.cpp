#include "SystemTasks.h"
#include <fstream>

DECLARE_CYCLE_STAT(TEXT("TaskSys: SyncPoint"), STAT_TS_SyncPoint, STATGROUP_ECS);
DECLARE_CYCLE_STAT(TEXT("TaskSys: GameTask"), STAT_TS_GameTask, STATGROUP_ECS);
DECLARE_CYCLE_STAT(TEXT("TaskSys: AsyncTask"), STAT_TS_AsyncTask, STATGROUP_ECS);


DECLARE_CYCLE_STAT(TEXT("TaskSys: TaskEnd1"), STAT_TS_End1, STATGROUP_ECS);

DECLARE_CYCLE_STAT(TEXT("TaskSys: TaskEnd2"), STAT_TS_End2, STATGROUP_ECS);
DECLARE_CYCLE_STAT(TEXT("TaskSys: WaitTime"), STAT_TS_Wait, STATGROUP_ECS);
DECLARE_CYCLE_STAT(TEXT("TaskSys: SyncLoop"), STAT_TS_SyncLoop, STATGROUP_ECS);

SystemTask* nextTask(SystemTask* task) {
	if (task->next) { return task->next; }
	else {	
		if (task->ownerGraph->next) {
			return (task->ownerGraph->next->firstTask);
		}		
	}
	return nullptr;
}

void GraphTask::BuildName() {
	if (original == nullptr) {

		TaskName = "EMPTY";
	}
	else {
		TaskName = original->ownerGraph->name + FString::FromInt(chainIndex);

		TaskName.ReplaceCharInline(' ', '_');
		TaskName.ReplaceCharInline(':', '_');
		TaskName.ReplaceCharInline('-', '_');
	}
	
}
void ECSSystemScheduler::AddTaskgraph(SystemTaskChain* newGraph)
{
	this->systasks.Add(newGraph);
}

void ECSSystemScheduler::Run(bool runParallel, ECS_Registry& reg)
{
	registry = &reg;
	systasks.Sort([](const SystemTaskChain& tskA, const  SystemTaskChain& tskB) {
		return tskA.sortKey < tskB.sortKey;
		});

	//fix dependencies
	auto find_index = [&](const FString& name) -> int {
		for (int i = 0; i < systasks.Num(); i++) {
			if (systasks[i]->name == name) return i;
		}
		return 0;
	};

	for (int i = 0; i < systasks.Num() - 1; i++) {
		if (systasks[i]->SystemDependencies.Num() > 0) {
			int minindex = 0;
			//find the min index we need this task to be at
			for (auto& n : systasks[i]->SystemDependencies)
			{
				minindex = FMath::Max(minindex, find_index(n));
			}

			//the task is below its dependencies
			if (minindex > i) {
				//reshuffle the array
				SystemTaskChain* movechain = systasks[i];

				//move all one down to cover the slot
				for (int j = i; j <= minindex; j++) {
					systasks[j] = systasks[j + 1];
				}
				systasks[minindex] = movechain;

				i--;
			}
		}
	}


	//schedule all the taskchains into a proper task graph
	TArray<GraphTask*> createdTasks;

	//auto find_last_task_of_system = [&](const FString& name) -> GraphTask* {
	//
	//}
	GraphTask* rootTask = NewGraphTask(nullptr);

	GraphTask* lastSyncTask = rootTask;

	TArray<GraphTask*> unconnectedTasks;

	for (int i = 0; i < systasks.Num(); i++)
	{
		SystemTaskChain* chain = systasks[i];

		int cindex = 0;
		GraphTask* task = NewGraphTask(chain->firstTask);
		task->chainIndex = cindex;
		lastSyncTask->AddSuccesor(task);
	
		while (task) {
			cindex++;
			if (task->original->type == ESysTaskType::SyncPoint)
			{
				//connect the live fatherless tasks to this
				for (auto t : unconnectedTasks) {
					t->AddSuccesor(task);
				}
				unconnectedTasks.Empty();

				lastSyncTask = task;
			}

			if (task->original->next) {
				GraphTask* newtask = NewGraphTask(task->original->next);
				newtask->chainIndex = cindex;
				task->AddSuccesor(newtask);
				task = newtask;
			}
			else {
						
				unconnectedTasks.Add(task);
				break;
			}

			
		}
	}


	//graphviz
	FString gviz = "digraph G { \n";
	for (auto t : AllocatedGraphTasks) {
		t->BuildName();
	}


	


	for (int i = 0; i < systasks.Num(); i++)
	{
		SystemTaskChain* chain = systasks[i];
		for (auto dep : chain->SystemDependencies)
		{
			//connect the first task of this to the last of dependencies

			GraphTask* chaintask = nullptr;
			for (auto t : AllocatedGraphTasks) {
				if (t && t->original == chain->firstTask)
				{
					chaintask = t; break;
				}
			}

			GraphTask* deptask = nullptr;

			for (int j = 0; j < systasks.Num(); j++) {
				if (systasks[j]->name == dep) {

					SystemTaskChain* depchain = systasks[j];

					for (auto t : AllocatedGraphTasks) {
						if (t && t->original == depchain->lastTask)
						{
							deptask = t; break;
						}
					}

					if (deptask) break;
				}
			}
			bool bfound = false;
							
			for (auto succ : deptask->successors)
			{
				if (succ == chaintask)
				{
					bfound = true;
					break;
				}
			}
			if (!bfound) {

				deptask->AddSuccesor(chaintask);
			}
		}
	}
	if (false)
	{
		std::ofstream f("E:/ECS_SpaceBattle/graphoutput.txt");

		//f.open();
		for (auto t : AllocatedGraphTasks) {


			FString strcolor = "[color = red,style = filled]; \n";
			if (t->original) {
				switch (t->original->type) {
				case ESysTaskType::GameThread: strcolor = "[color = brown,style = filled];\n"; break;
				case ESysTaskType::SyncPoint:strcolor = "[color = red,style = filled];\n"; break;
				case ESysTaskType::FreeTask:strcolor = "[color = blue];\n"; break;
				}
			}
			FString line = "\"" + t->TaskName + "\"" + strcolor;// [sides = 9, distortion = "0.936354", orientation = 28, skew = "-0.126818", color = salmon2];

			line += "\"" + t->TaskName + "\"" + " -> { ";

			for (auto sc : t->successors) {
				line += "\"" + sc->TaskName + "\"" + " ,";
			}
			line.RemoveFromEnd(",");
			line += " }; \n";

			gviz += line;
		}
		gviz += "}";

		std::string ostr(TCHAR_TO_UTF8(GetData(gviz)));
		f << ostr;
		f.close();
	}
	
	if (!runParallel) {

		TArray<GraphTask*> _pendingTasks;

		for (auto t : rootTask->successors) {
			
			t->predecessorCount--;
			if (t->predecessorCount == 0) {
				_pendingTasks.Add(t);
			}
		}

		while (_pendingTasks.Num() > 0)
		{
			for (auto t : _pendingTasks) {
				t->original->function(reg);
			}

			TArray<GraphTask*> newTasks;

			for (auto t : _pendingTasks) {
				for (auto nxt : t->successors) {
					
					nxt->predecessorCount--;
					if (nxt->predecessorCount == 0) {
						newTasks.Add(nxt);
					}
				}
			}

			//_pendingTasks.Empty();
			_pendingTasks = newTasks;
		}

		//for (auto s : systasks) {
		//	s->ExecuteSync(reg);
		//}
	}
	else {

		endEvent = FPlatformProcess::GetSynchEventFromPool(false);


		totalTasks = AllocatedGraphTasks.Num();

		AsyncFinished(rootTask);

		int loopcounts = 0;
		bool breakloop = false;
		while (totalTasks > 0  && !breakloop) {


			GraphTask* gametask;
			while (gameTasks.Dequeue(gametask))
			{
			//	UE_LOG(LogFlying, Warning, TEXT("MTXLOCK:Gametask"));
				endmutex.Lock();

				bool bCanExecute = CanExecute(gametask);
				

				if (bCanExecute) {
					
					AddPending(gametask, nullptr);

					//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK:Gametask"));
					endmutex.Unlock();

					//UE_LOG(LogFlying, Warning, TEXT("Executing game task: %s"), *gametask->TaskName);

					SCOPE_CYCLE_COUNTER(STAT_TS_GameTask);
					gametask->original->function(reg);

					AsyncFinished(gametask);
				}
				else {
					//back to the queue
					waitingTasks.Add(gametask);
					//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK: %s"), "Gametask");
					endmutex.Unlock();					
				}
			}
			//UE_LOG(LogFlying, Warning, TEXT("MTXLOCK: SyncLaunch0"));
			endmutex.Lock();

			if(pendingTasks.Num() == 0)
			{
				SCOPE_CYCLE_COUNTER(STAT_TS_SyncPoint);
				GraphTask* sync = nullptr;

				sync = syncTask;

				while (sync) {

					//UE_LOG(LogFlying, Warning, TEXT("Executing Root task: %s"), *sync->TaskName);

					sync->original->function(reg);
					syncTask = nullptr;
					//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK: SyncLaunch"));
					//endmutex.Unlock();
					AsyncFinished(sync);
					//UE_LOG(LogFlying, Warning, TEXT("MTXLOCK: SyncLaunch"));
					//endmutex.Lock();
					sync = nullptr;
					if (syncTask != nullptr)
					{
						sync = syncTask;
						
					}
					else
					{
					//	UE_LOG(LogFlying, Warning, TEXT("MTXLOCK: SyncLaunch2"));
						//endmutex.Lock();
					}
				};
			}			

			//if (pendingTasks.Num() == 0)
			//{
				//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK: FinalSync"));
				endmutex.Unlock();
				endEvent->Wait(1);		
				loopcounts++;
				if (loopcounts > 100) {
					//UE_LOG(LogFlying, Warning, TEXT("Shits busted"));
					breakloop = true;
					//return;
				}
			//}


		}

		#if 0//connect the tasks into a linked list
		for (int i = 0; i < systasks.Num() - 1; i++) {
			systasks[i]->next = systasks[i + 1];
		}
		
		//find how many tasks in total
		tasksUntilSync = 0;
		totalTasks = 0;
		SystemTask* itTask = systasks[0]->firstTask;
		while (itTask) {
			totalTasks++;
			itTask = nextTask(itTask);
		}
		
		
		//run the initial sync-d tasks
		itTask = systasks[0]->firstTask;
		while (itTask) {
			if (itTask->type != ESysTaskType::SyncPoint) {
				break;
			}
			else {
				SCOPE_CYCLE_COUNTER(STAT_TS_SyncPoint);
				//UE_LOG(LogFlying, Warning, TEXT("Executing sync task: %s"), *itTask->ownerGraph->name);
		
				itTask->function(reg);
				itTask = nextTask(itTask);
				totalTasks--;
			}
		}
		//bool bcontinue = true;
		while (totalTasks > 0) {
		
			//find how many tasks until first sync, and gather initial tasks
			//itTask = systasks[0]->firstTask;
			tasksUntilSync = 0;
		
			//run the initial sync-d tasks		
			while (itTask) {
				if (itTask->type != ESysTaskType::SyncPoint) {
					break;
				}
				else {
					SCOPE_CYCLE_COUNTER(STAT_TS_SyncPoint);
		
					//UE_LOG(LogFlying, Warning, TEXT("Executing sync task: %s"), *itTask->ownerGraph->name);
		
		
					itTask->function(reg);
					itTask = nextTask(itTask);
					totalTasks--;
				}
			}
		
			if (totalTasks == 0) return;
		
			SystemTask* checktsk = itTask;
			TArray<SystemTask*> initialTasks;
		
			if (checktsk->ownerGraph->firstTask != checktsk) {
				initialTasks.Add(checktsk);
			}
		
		
			while (checktsk) {
				if (checktsk->type == ESysTaskType::SyncPoint) {
					syncTask = checktsk;
					break;
				}
				if (checktsk->ownerGraph->firstTask == checktsk) {
					initialTasks.Add(checktsk);
				}
				tasksUntilSync++;
				checktsk = nextTask(checktsk);
			}
		
			//launch initial tasks
			for (auto t : initialTasks) {
				ExecuteTask(t);
			}
		
			while (tasksUntilSync.Load(EMemoryOrder::Relaxed) > 0) {
		
				SCOPE_CYCLE_COUNTER(STAT_TS_SyncLoop);
		
				SystemTask* gametask;
				while (gameTasks.Dequeue(gametask))
				{
					endmutex.Lock();
		
					bool bCanExecute = CanExecute(gametask);
					//endmutex.Unlock();
		
					if (bCanExecute) {
						//endmutex.Lock();
						AddPending(gametask,nullptr);
						endmutex.Unlock();
		
		
						//UE_LOG(LogFlying, Warning, TEXT("Executing game task: %s"), *gametask->ownerGraph->name);
		
		
						SCOPE_CYCLE_COUNTER(STAT_TS_GameTask);
						gametask->function(reg);
		
						AsyncFinished(gametask);
					}
					else {
						endmutex.Unlock();
						//back to the queue
						gameTasks.Enqueue(gametask);
		
						
					}
					
				}
				TArray<SystemTask*, TInlineAllocator<5>> executableTasks;
		
				SystemTask* tsk = nullptr;
				endmutex.Lock();
		
			
				if (waitingTasks.Num() != 0) {
		
					for (int i = 0; i < waitingTasks.Num(); i++) {
						if (CanExecute(waitingTasks[i])) {
							tsk = waitingTasks[i];
							waitingTasks.RemoveAt(i);
							executableTasks.Add(tsk);
							i--;
							//break;
						}
					}		
				}
				endmutex.Unlock();
		
				for (auto t : executableTasks) {
					ExecuteTask(t);
				}
				//if (tsk) {
				//
				//	if (!ExecuteTask(tsk)) {
				//		//special case, the task we just attempted to execute has 
				//	}
				//}
				//else 
				{
					//we ran out of stuff, wait a little bit
					//endmutex.Lock();
		
					endEvent->Wait();
					//pendingTasks[0]->future.Wait();
		
					//UE_LOG(LogFlying, Warning, TEXT("Post Wait Task"));
		
		
					//endmutex.Unlock();
				}
			}
		
			{
				SCOPE_CYCLE_COUNTER(STAT_TS_SyncPoint);
				syncTask->function(reg);
				itTask = nextTask(syncTask);
				totalTasks--;
			}
		}
#endif
	}
}
#if 0
void ECSSystemScheduler::AsyncFinished(SystemTask* task)
{
	SCOPE_CYCLE_COUNTER(STAT_TS_End1);

	//UE_LOG(LogFlying, Warning, TEXT("Task Finished: %s"), *task->ownerGraph->name);


	endmutex.Lock();

	tasksUntilSync--;
	totalTasks--;
	
	RemovePending(task);

	endmutex.Unlock();

	endEvent->Trigger();

	//trigger execution of next task
	if (task->next) {
		ExecuteTask(task->next);
	}
	//attempt execution of a pending task
	//else
	{
		SystemTask* tsk = nullptr;
		endmutex.Lock();
		TArray<SystemTask*, TInlineAllocator<5>> executableTasks;
		if (waitingTasks.Num() != 0) {

			for (int i = 0; i < waitingTasks.Num(); i++) {
				if (CanExecute(waitingTasks[i])) {
					tsk = waitingTasks[i];
					waitingTasks.RemoveAt(i);
					executableTasks.Add(tsk);
					i--;
					//break;
				}
			}
		}
		endmutex.Unlock();

		for (auto t : executableTasks) {
			ExecuteTask(t);
		}

		//if (waitingTasks.Num() != 0) {
		//	for (int i = 0; i < waitingTasks.Num(); i++) {
		//		if (CanExecute(waitingTasks[i])) {
		//			tsk = waitingTasks[i];
		//			waitingTasks.RemoveAt(i);
		//			break;
		//		}
		//	}
		//}
		//endmutex.Unlock();
		//
		//if (tsk) {
		//	ExecuteTask(tsk);
		//}	

	}
	//if (task->next) {
	//	waitingTasks.Add(task->next);
	//}
	//else {
	//	if (task->ownerGraph->next) {
	//		waitingTasks.Add(task->ownerGraph->next->firstTask);
	//	}
	//}	

	
}
#endif
void ECSSystemScheduler::AsyncFinished(GraphTask* task)
{
	SCOPE_CYCLE_COUNTER(STAT_TS_End1);

	
	//UE_LOG(LogFlying, Warning, TEXT("Task Finished: %s"), *task->TaskName);

	//UE_LOG(LogFlying, Warning, TEXT("MTXLOCK:AsyncFinished0"));
	endmutex.Lock();


	
	totalTasks--;
	if (task->original)
	{
		RemovePending(task);
	}
	//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK:AsyncFinished0"));
	endmutex.Unlock();

	endEvent->Trigger();

	//trigger execution of next task

	

	//if (task->next) {
	//	ExecuteTask(task->next);
	//}
	//attempt execution of a pending task
	
	{
		//UE_LOG(LogFlying, Warning, TEXT("MTXLOCK: AsyncFinished1"));
		endmutex.Lock();
		TArray<GraphTask*, TInlineAllocator<5>> executableTasks;
		


		for (auto nxt : task->successors) {

			nxt->predecessorCount--;
			if (nxt->predecessorCount == 0) {
			//	if (CanExecute(nxt)) {

					//UE_LOG(LogFlying, Warning, TEXT("ADD WAITING %s"), *nxt->TaskName);
					waitingTasks.Add(nxt);
			//	}
			}
		}
		
		if (waitingTasks.Num() != 0) {

			TArray<int, TInlineAllocator<5>> indicesToRemove;

			for (int i = 0; i < waitingTasks.Num(); i++) {
				if (CanExecute(waitingTasks[i])) {
					GraphTask* tsk = waitingTasks[i];
					//waitingTasks.RemoveAt(i);
					executableTasks.Add(tsk);
					indicesToRemove.Add(i);					
					//i--;					
				}
			}
			for (auto idx : indicesToRemove)
			{
				waitingTasks[idx] = nullptr;
			}

			waitingTasks.Remove(nullptr);

		}	
		//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK: AsyncFinished1"));
		endmutex.Unlock();

		for (auto t : executableTasks) {

			//UE_LOG(LogFlying, Warning, TEXT("Launching task: %s"), *t->TaskName);
			LaunchTask(t);
		}
	}
}

bool ECSSystemScheduler::LaunchTask(GraphTask* task)
{
	SCOPE_CYCLE_COUNTER(STAT_TS_End2);

	//UE_LOG(LogFlying, Warning, TEXT("MTXLOCK:LaunchTask"));
	endmutex.Lock();
	ESysTaskType tasktype = task->original->type;
	if (tasktype == ESysTaskType::SyncPoint) {
		syncTask = task;
		endmutex.Unlock(); 
		
		return true;
	}
	else //if (tasktype == ESysTaskType::FreeTask || tasktype == ESysTaskType::GameThread)
		{
			if (!CanExecute(task))
			{
				waitingTasks.Add(task);
				//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK: LaunchTask"));
				endmutex.Unlock();
				return false;
			}
			else {

				EAsyncExecution exec;
				if (tasktype == ESysTaskType::FreeTask)
				{
					exec = EAsyncExecution::TaskGraph;

					TFuture<void> fut = Async(exec, [=]() {
						SCOPE_CYCLE_COUNTER(STAT_TS_AsyncTask);
						task->original->function(*registry);
						AsyncFinished(task);
						},

						[=]() {

						}
						);

					AddPending(task, &fut);
					//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK:LaunchTask1"));
					endmutex.Unlock();
				}
				else {
					//UE_LOG(LogFlying, Warning, TEXT("MTXUNLOCK:LaunchTask1"));
					endmutex.Unlock();
					gameTasks.Enqueue(task);
				}

				
				return true;
			}
		}
	return false;
}
#if 0
bool ECSSystemScheduler::ExecuteTask(SystemTask* task)
{
	SCOPE_CYCLE_COUNTER(STAT_TS_End2);
	endmutex.Lock();
	if (task->type == ESysTaskType::SyncPoint) {
		//syncpoints are special case
		//assert(false);
		//endmutex.Lock();
		//syncTask = task;		
	}
	else if (task->type == ESysTaskType::FreeTask || task->type == ESysTaskType::GameThread) {
		//check dependencies
		
		
		if (!CanExecute(task))
		{
			waitingTasks.Add(task);
			endmutex.Unlock();
			return false;
		}
		else{

			EAsyncExecution exec;
			if (task->type == ESysTaskType::FreeTask)
			{
				exec = EAsyncExecution::TaskGraph;

				//UE_LOG(LogFlying, Warning, TEXT("launching AsyncTask: %s"), *task->ownerGraph->name);


				TFuture<void> fut = Async(exec, [=]() {
					SCOPE_CYCLE_COUNTER(STAT_TS_AsyncTask);
					task->function(*registry);
					AsyncFinished(task);
					},
					//when it finishes					
					[=]() {
						
					}
					);

				AddPending(task,&fut);
				//TSharedPtr<LaunchedTask> newTask = MakeShared<LaunchedTask>();
				//
				//
				//newTask->task = task;
				//newTask->future = std::move(fut);
				//newTask->dependencies = task->deps;
				//
				////endmutex.Lock();
				//pendingTasks.Add(newTask);
			}
			else {				
				gameTasks.Enqueue(task);
			}

		
			//endmutex.Unlock();
		}
		//endmutex.Unlock();
	}
	
	endmutex.Unlock();
	return true;
}

void ECSSystemScheduler::AddPending(SystemTask* task, TFuture<void> *future)
{
	TSharedPtr<LaunchedTask> newTask = MakeShared<LaunchedTask>();


	newTask->task = task;
	if (future) {
		newTask->future = std::move(*future);
	}
	
	newTask->dependencies = task->deps;
	
	pendingTasks.Add(newTask);
}


void ECSSystemScheduler::RemovePending(SystemTask* task)
{
	//UE_LOG(LogFlying, Warning, TEXT("Task Removed: %s"), *task->ownerGraph->name);
	//remove from pending tasks
	for (int i = 0; i < pendingTasks.Num(); i++) {
		if (pendingTasks[i]->task == task) {
			pendingTasks.RemoveAt(i);
			break;
		}
	}
}
bool ECSSystemScheduler::CanExecute(SystemTask* task)
{
	for (auto& t : pendingTasks) {
		if (t->dependencies.ConflictsWith(task->deps)) {
			return false;
		}
	}
	return true;
}
#endif
void ECSSystemScheduler::RemovePending(GraphTask* task)
{
	//remove from pending tasks
	for (int i = 0; i < pendingTasks.Num(); i++) {
		if (pendingTasks[i]->task == task) {
			pendingTasks.RemoveAt(i);
			break;
		}
	}
}
void ECSSystemScheduler::AddPending(GraphTask* task, TFuture<void>* future)
{
	TSharedPtr<LaunchedTask> newTask = MakeShared<LaunchedTask>();

	newTask->task = task;
	if (future) {
		newTask->future = std::move(*future);
	}

	//UE_LOG(LogFlying, Warning, TEXT("ADD PENDING %s"),*task->TaskName);
	newTask->dependencies = task->original->deps;

	pendingTasks.Add(newTask);
}

bool ECSSystemScheduler::CanExecute(GraphTask* task)
{
	for (auto& t : pendingTasks) {
		if (t->dependencies.ConflictsWith(task->original->deps)) {
			return false;
		}
	}
	return true;
}

SystemTask* ECSSystemScheduler::NewTask()
{
	SystemTask* taks = new SystemTask();

	AllocatedTasks.Add(taks);

	return taks;
}

GraphTask* ECSSystemScheduler::NewGraphTask(SystemTask* originalTask)
{
	GraphTask* taks = new GraphTask();
	taks->original = originalTask;
	taks->predecessorCount = 0;
	AllocatedGraphTasks.Add(taks);

	return taks;
}

