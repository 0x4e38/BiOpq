#include "llvm/Transforms/Obfuscation/MatrixBranchProg.h"
#include "matrix/MatUtils.h"

using namespace llvm;
using namespace std;

int mod = 256;

class MatrixInIR{

private:
  int64_t width = 0;
  int64_t height = 0;
  BasicBlock* pBB;
  BasicBlock* nextBB;
  BasicBlock* continueBB;
  LLVMContext& context;
  Module& module;
  AllocaInst* matAI;

public:

  MatrixInIR(Module& module, LLVMContext& context, BasicBlock* pBB, BasicBlock* nextBB, int64_t** mat, const int64_t height, const int64_t width)
	  :module(module), context(context), pBB(pBB), nextBB(nextBB), height(height), width(width){
    LOG(L2_DEBUG) << "Constructing MatrixInIR...";

    ConstantInt* ci8Height = (ConstantInt*) ConstantInt::getSigned(i64Type, height * 8);
	matAI = new AllocaInst(l2Ptr_64,"", pBB);
    Instruction* l1MI = CallInst::CreateMalloc(matAI, i64Type, i8Type, ci8Height, nullptr, (Function*) mallocFunc, "l1_malloc");
	BitCastInst* l1MBI = new BitCastInst(l1MI, l2Ptr_64, "", pBB);
	StoreInst* l1MSI = new StoreInst(l1MBI, matAI, pBB);

    //Create the blocks
	ConstantInt* ciHeight = ConstantInt::getSigned(i64Type, height);
	ConstantInt* ciWidth = ConstantInt::getSigned(i64Type, width);
	AllocaInst* iAI = new AllocaInst(i64Type, "", pBB);
	StoreInst* iSI = new StoreInst(ci0_64, iAI, pBB);
	AllocaInst* hAI = new AllocaInst(i64Type, "", pBB);
	StoreInst* hSI = new StoreInst(ciHeight, hAI, pBB);
	AllocaInst* wAI = new AllocaInst(i64Type, "", pBB);
	StoreInst* wSI = new StoreInst(ciWidth, wAI, pBB);
	Instruction* fakeBI = BranchInst::Create(nextBB, pBB);

    BasicBlock* fCondBB = pBB->splitBasicBlock(fakeBI, "for_cond");
    BasicBlock* fBodyBB = fCondBB->splitBasicBlock(fakeBI, "for_body");
    BasicBlock* fIncBB = fBodyBB->splitBasicBlock(fakeBI, "for_inc");
    BasicBlock* fEndBB = fIncBB->splitBasicBlock(fakeBI, "for_end");
    continueBB = fEndBB->splitBasicBlock(fakeBI, "continue");
    fCondBB->getTerminator()->eraseFromParent();
    fBodyBB->getTerminator()->eraseFromParent();
    fIncBB->getTerminator()->eraseFromParent();
	

	LoadInst* iLI = new LoadInst(iAI, "", fCondBB);
	LoadInst* hLI = new LoadInst(hAI, "", fCondBB);
    ICmpInst* forCI = new ICmpInst(*fCondBB, CmpInst::ICMP_SLT, iLI, hLI, "");
    BranchInst::Create(fBodyBB, fEndBB, forCI, fCondBB);

	ConstantInt* ci8Width = ConstantInt::getSigned(i64Type, width * 8);
	LoadInst* matLI = new LoadInst(matAI,"", fBodyBB);
	Instruction* l2MI = CallInst::CreateMalloc(matLI, i64Type, i8Type, ci8Width, nullptr, (Function*) mallocFunc, "l2_malloc");
	BitCastInst* l2MBI = new BitCastInst(l2MI, i64PT, "", fBodyBB);
	GetElementPtrInst* l1EPI = GetElementPtrInst::CreateInBounds(matLI, iLI,"", fBodyBB);
    StoreInst* l2MSI = new StoreInst(l2MBI, l1EPI, fBodyBB);
	BranchInst::Create(fIncBB, fBodyBB);

    BinaryOperator* iAdd = BinaryOperator::CreateNSWAdd((Value*) iLI, ci1_64, "", fIncBB);
    StoreInst* iFiSI = new StoreInst(iAdd, iAI, fIncBB);
    BranchInst::Create(fCondBB, fIncBB);

	//continueBB, we shoud use an instruction as the place anchor, so that the original terminator would not get changed 
	for(int64_t i=0; i<height; i++){
	  ConstantInt* cii = ConstantInt::getSigned(i64Type, i);
	  LoadInst* matLI2 = new LoadInst(matAI,"", fakeBI);
 	  GetElementPtrInst* l1EPI = GetElementPtrInst::CreateInBounds(matLI2, cii,"", fakeBI);
	  for(int64_t j=0; j<width; j++){	
		ConstantInt* cij = (ConstantInt*) ConstantInt::getSigned(i64Type,j);
		LoadInst* l1LI = new LoadInst(l1EPI,"", fakeBI);
		GetElementPtrInst* l2EPI = GetElementPtrInst::CreateInBounds(l1LI, cij,"", fakeBI);
		ConstantInt* i64Tmp = (ConstantInt*) ConstantInt::get(i64Type, mat[i][j]);
		StoreInst* storeInst = new StoreInst(i64Tmp, l2EPI, fakeBI);
	  }
	}

    fakeBI->eraseFromParent();
    LOG(L2_DEBUG) << "Constructing MatrixInIR...Done";
  }

  AllocaInst* getMatAI(){
	return matAI; 
  }

  BasicBlock* getBB(){
	return continueBB;
  }
  int64_t getWidth(){
	return width;
  }

  int64_t getHeight(){
	return height;
  }

  ~MatrixInIR(){
  }

};


void ConvertIcmp2Mbp(Module& module, ICmpInst *icmpInst){
  LOG(L2_DEBUG) << "ConvertIcmp2Mbp...";
  //Init the context;
  LLVMContext& context = icmpInst->getContext();
  BasicBlock* pBB = icmpInst->getParent();

  //Init arrayref as the index to access the array.
  ArrayRef<Value*> ar00_64(vec00_64);
  ArrayRef<Value*> ar01_64(vec01_64);

  //Create the blocks
  BasicBlock* forCondBB = pBB->splitBasicBlock(icmpInst, "for_cond_loop");
  pBB->getTerminator()->eraseFromParent();
  BasicBlock* forBodyBB = forCondBB->splitBasicBlock(icmpInst, "for_body_loop");
  forCondBB->getTerminator()->eraseFromParent();
  BasicBlock* forIncBB = forBodyBB->splitBasicBlock(icmpInst, "for_inc_loop");
  forBodyBB->getTerminator()->eraseFromParent();
  BasicBlock* forEndBB = forIncBB->splitBasicBlock(icmpInst, "for_end_loop");
  forIncBB->getTerminator()->eraseFromParent();
  BasicBlock* conBB = forEndBB->splitBasicBlock(icmpInst, "entry_continue");


  //Check the icmp instruction; Currently, we only precess ICmp with Equal operator and exactly one constant side.
  Instruction::OtherOps opCode = icmpInst->getOpcode();
  //Type* type = icmpInst->getType();
  //CmpInst::Predicate predicate = icmpInst->getPredicate();
  Value* op0 = icmpInst->getOperand(0);
  Value* op1 = icmpInst->getOperand(1);
  if(!(isa<ConstantInt> (*op0) || isa<ConstantInt>(*op1))){
	LOG(L_INFO) << "[-PI-]:ICmpInst has two symbolic variables; Don't obfuscate!";
  }
  IntegerType* inpVar; //the variable side 
  ConstantInt* ciCmpObj; // the constant side

  if(isa<ConstantInt> (*op0)){
	ciCmpObj = (ConstantInt*)op0;
	inpVar = (IntegerType*) op1;
  }
  else{	
	ciCmpObj = (ConstantInt*) op1;
	inpVar = (IntegerType*) op0;
  }

  int64_t len = ciCmpObj->getBitWidth();
  int64_t dim = len + 2; //the dimension of matrix;
  LOG(L_INFO) << "LEN:"<<len;

  //ArrayType* l1At_l2Ptr = ArrayType::get(l2Ptr_64, len);
  //ArrayType* l2At_l2Ptr = ArrayType::get(l1At_l2Ptr, 2);


  ConstantInt* ciDim = (ConstantInt*) ConstantInt::getSigned(i64Type,dim);
  ConstantInt* ciLen = (ConstantInt*) ConstantInt::getSigned(i64Type,len);
  ConstantInt* ci8Dim = (ConstantInt*) ConstantInt::getSigned(i64Type,8*dim);
  ConstantInt* ciSize = (ConstantInt*) ConstantInt::getSigned(i64Type, dim*dim);

  AllocaInst* matAI = new AllocaInst(l2Ptr_64, ciSize, "matrix", pBB);
  //Get the pointers of matrix for input bit 0
  GetElementPtrInst* mat0PtrEPI = GetElementPtrInst::CreateInBounds(matAI, ci0_64,"", pBB);
  //Get the pointers of matrix for input bit 1 
  GetElementPtrInst* mat1PtrEPI = GetElementPtrInst::CreateInBounds(matAI, ci1_64,"", pBB);

  const char strArg_1[] = "ICmp constant: %d\n";
  PrintInIR(module, pBB, strArg_1, sizeof(strArg_1), ciCmpObj);

  //Generate the matrix and save to matAI
  int64_t** headMat = (int64_t**) malloc (sizeof(int64_t*) * 1);
  int64_t** headMatRand = (int64_t**) malloc (sizeof(int64_t*) * 1);
  int64_t** tailMat = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** tailMatRand = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** midMat0 = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** midMat0Rand = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** midMat1 = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** midMat1Rand = (int64_t**) malloc (sizeof(int64_t*) * dim);

  headMat[0] = (int64_t*) malloc (sizeof(int64_t) * dim);
  headMatRand[0] = (int64_t*) malloc (sizeof(int64_t) * dim);
  for(int64_t i=0; i<dim; i++){
    tailMat[i] = (int64_t*) malloc (sizeof(int64_t) * 1);
    tailMatRand[i] = (int64_t*) malloc (sizeof(int64_t) * 1);
	midMat0[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	midMat0Rand[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	midMat1[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	midMat1Rand[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
  }

  CreateIntMat(headMat, 1, dim, 0, 0);
  
  int64_t** randMat = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** randMatInv = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** randMat2 = (int64_t**) malloc (sizeof(int64_t*) * dim);
  int64_t** randMatInv2 = (int64_t**) malloc (sizeof(int64_t*) * dim);
  for (int64_t i = 0; i < dim; i++)
  {
	randMat[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	randMatInv[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	randMat2[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
	randMatInv2[i] = (int64_t*) malloc (sizeof(int64_t) * dim);
  }
  
  GenIntMatPair(randMat, randMatInv, dim, mod);
  MultIntMatrix(headMat, randMat, headMatRand, 1, dim, dim, dim, mod);
  MatrixInIR* headMatIR = new MatrixInIR(module, context, pBB, conBB, headMatRand, 1, dim); //~bit
  pBB = headMatIR->getBB();

  int64_t iRow;
  int64_t iCol4T;
  int64_t iCol4F = len;
  for(int64_t i=0; i<len; i++){
	bool inpBit = ciCmpObj->getValue()[i];//It starts from the lower bit.
	LOG(L1_DEBUG)<<"+++++++++++++++++++++++++++++++Inp["<<i<<"]:"<<inpBit;

    iRow = i;

	if(i < len-1){
	  iCol4T = i+1;
	}
	else{
	  iCol4T = i+2;
	}

	if(inpBit){
	  CreateIntMat(midMat0,dim,dim,iRow,iCol4F);
	  CreateIntMat(midMat1,dim,dim,iRow,iCol4T);
	} else{
	  CreateIntMat(midMat0,dim,dim,iRow,iCol4T);
	  CreateIntMat(midMat1,dim,dim,iRow,iCol4F);
	}
	if(i==0){
	  MultIntMatrix(randMatInv, midMat0, midMat0Rand, dim, dim, dim, dim, mod);
	  MultIntMatrix(randMatInv, midMat1, midMat1Rand, dim, dim, dim, dim, mod);
	}
	else{
	  MultIntMatrix(randMatInv2, midMat0, midMat0Rand, dim, dim, dim, dim, mod);
	  MultIntMatrix(randMatInv2, midMat1, midMat1Rand, dim, dim, dim, dim, mod);
	}

    CopyIntMat(midMat0, midMat0Rand, dim, dim); 
    CopyIntMat(midMat1, midMat1Rand, dim, dim); 

    GenIntMatPair(randMat2, randMatInv2, dim, mod);
    //PrintIntMat(randMat2, dim, dim);
    //PrintIntMat(randMatInv2, dim, dim);
    MultIntMatrix(midMat0, randMat2, midMat0Rand, dim, dim, dim, dim, mod);
    MultIntMatrix(midMat1, randMat2, midMat1Rand, dim, dim, dim, dim, mod);

	MatrixInIR*  mat0IR = new MatrixInIR(module, context, pBB, conBB, midMat0Rand, dim,dim); //bit
    pBB = mat0IR->getBB();
	MatrixInIR*  mat1IR = new MatrixInIR(module, context, pBB, conBB, midMat1Rand, dim,dim); //~bit
    pBB = mat1IR->getBB();
    //PrintIntMat(midMat0, dim, dim);
    //PrintIntMat(midMat1, dim, dim);

    ConstantInt* cii = (ConstantInt*) ConstantInt::getSigned(i64Type,i);
    vector<Value*> vec0i;
    vec0i.push_back(ci0_64);
    vec0i.push_back(cii);
    ArrayRef<Value*> ar0i(vec0i);

  LOG(L2_DEBUG) << "TAG:+++++++++++++++++++++++++++++++++==1..";
    GetElementPtrInst* mat0EPI = GetElementPtrInst::CreateInBounds(mat0PtrEPI, cii,"", pBB);
    GetElementPtrInst* mat1EPI = GetElementPtrInst::CreateInBounds(mat1PtrEPI, cii,"", pBB);

	LoadInst* mat0LI = new LoadInst(mat0IR->getMatAI()," ", pBB);
	LoadInst* mat1LI = new LoadInst(mat1IR->getMatAI()," ", pBB);
    StoreInst* mat0StoreInst = new StoreInst(mat0LI, mat0EPI, pBB);
    StoreInst* mat1StoreInst = new StoreInst(mat1LI, mat1EPI, pBB);
  }

  LOG(L2_DEBUG) << "TAG:+++++++++++++++++++++++++++++++++==2..";
  CreateIntMat(tailMat, dim, 1, dim-1, 0);
  MultIntMatrix(randMatInv2, tailMat, tailMatRand, dim, dim, dim, 1, mod);
  //EncMatrix(tailMatRand,dim,1,dim);
  MatrixInIR* tailMatIR = new MatrixInIR(module, context, pBB, conBB, tailMatRand, dim, 1); //bit
  pBB = tailMatIR->getBB();
  //PrintIntMat(tailMat, dim, 1);

  LOG(L2_DEBUG) << "TAG:+++++++++++++++++++++++++++++++++==3..";
  //Init the parameter for the for loop; 
  AllocaInst* iAI = new AllocaInst(i64Type,"", pBB);
  StoreInst* iSI = new StoreInst(ci1_64, iAI, pBB);
  AllocaInst* lenAllocaInst = new AllocaInst(i64Type,"", pBB);
  StoreInst* lenStoreInst = new StoreInst(ciLen, (Value *) lenAllocaInst, pBB);

  AllocaInst* matIdAI = new AllocaInst(i64Type,"mat_idx", pBB);
  BinaryOperator* andBO = BinaryOperator::Create(Instruction::And, (Value *) inpVar, ci1_64, "", pBB);
  StoreInst* matIdSI = new StoreInst((Value *) andBO, matIdAI, pBB);
  LoadInst* matIdLI = new LoadInst((Value *) matIdAI, "", pBB);
  
  const char strArg_IdLI[] = "Choose mat for inp: %d\n";
  //PrintInIR(module, pBB, strArg_IdLI, sizeof(strArg_IdLI), matIdLI);

  vector<Value*> vec0inp;
  vec0inp.push_back(ci0_64);
  vec0inp.push_back(matIdLI);
  ArrayRef<Value*> ar0inp(vec0inp);
  GetElementPtrInst* getBinEPI = GetElementPtrInst::CreateInBounds(matAI, matIdLI,"", pBB);
  GetElementPtrInst* getLenEPI = GetElementPtrInst::CreateInBounds(getBinEPI, ci0_64,"", pBB);
  LoadInst* ldMatLI = new LoadInst(getLenEPI,"",pBB);

  GetElementPtrInst* headMatEPI = GetElementPtrInst::CreateInBounds(headMatIR->getMatAI(), ci0_64,"", pBB);
  BitCastInst* headMatBI = new BitCastInst((Value*) headMatEPI, l2Ptr_64, "", pBB);
  AllocaInst* headMatAI = new AllocaInst(l2Ptr_64,"", pBB);
  StoreInst* headMatSI = new StoreInst(headMatBI, (Value *) headMatAI, pBB);
  LoadInst* headMatLI = new LoadInst(headMatAI,"", pBB);

  // Allocate two tmp matrix: height = 1; width = dim;
  AllocaInst* iMatAI = new AllocaInst(l2Ptr_64,"iMat",pBB);
  AllocaInst* iMat2AI = new AllocaInst(l2Ptr_64,"iMat2",pBB);

  //CreateMalloc (BasicBlock *InsertAtEnd, Type *IntPtrTy, Type *AllocTy, Value *AllocSize, Value *ArraySize=nullptr, Function *MallocF=nullptr, const Twine &Name="")
  Instruction* mallocI11 = CallInst::CreateMalloc(iMatAI, i64Type, i8Type, ci8_64, nullptr, (Function*) mallocFunc, "mallocCall11");
  BitCastInst* mallocBI11 = new BitCastInst(mallocI11, l2Ptr_64, "", pBB);
  StoreInst* mallocSI11 = new StoreInst(mallocBI11, iMatAI, pBB);

  Instruction* mallocI12 = CallInst::CreateMalloc(iMatAI, i64Type, i8Type, ci8Dim, nullptr, (Function*) mallocFunc, "mallocCall12");
  BitCastInst* mallocBI12 = new BitCastInst(mallocI12, i64PT, "", pBB);
  LoadInst* iMatLI = new LoadInst(iMatAI,"", pBB);
  GetElementPtrInst* iMatEPI1 = GetElementPtrInst::CreateInBounds(iMatLI, ci0_64,"", pBB);
  StoreInst* mallocSI12 = new StoreInst(mallocBI12, iMatEPI1, pBB);

  Instruction* mallocI21 = CallInst::CreateMalloc(iMat2AI, i64Type, i8Type, ci8_64, nullptr, (Function*) mallocFunc, "mallocCall21");
  BitCastInst* mallocBI21 = new BitCastInst(mallocI21, l2Ptr_64, "", pBB);
  StoreInst* mallocSI21 = new StoreInst(mallocBI21, iMat2AI, pBB);

  Instruction* mallocI22 = CallInst::CreateMalloc(iMat2AI, i64Type, i8Type, ci8Dim, nullptr, (Function*) mallocFunc, "mallocCall22");
  BitCastInst* mallocBI22 = new BitCastInst(mallocI22, i64PT, "", pBB);
  LoadInst* iMat2LI = new LoadInst(iMat2AI,"", pBB);
  GetElementPtrInst* iMatEPI2 = GetElementPtrInst::CreateInBounds(iMat2LI, ci0_64,"", pBB);
  StoreInst* mallocSI22 = new StoreInst(mallocBI22, iMatEPI2, pBB);

  ConstantInt* ciMod = (ConstantInt*) ConstantInt::getSigned(i64Type, mod);
  vector<Value*> vecMM;
  vecMM.push_back(headMatLI);
  vecMM.push_back(ldMatLI);
  vecMM.push_back(iMat2LI);
  vecMM.push_back(ci1_64);
  vecMM.push_back(ciDim);
  vecMM.push_back(ciDim);
  vecMM.push_back(ciDim);
  vecMM.push_back(ciMod);
  ArrayRef<Value*> arMM(vecMM);

  CallInst* mmCI = CallInst::Create(multMatFunc, arMM, "", pBB);

  AllocaInst* swapMatAI = new AllocaInst(l2Ptr_64,"swapMat",pBB);
  StoreInst* mallocSI1 = new StoreInst(iMat2LI, swapMatAI, pBB);
  StoreInst* mallocSI2 = new StoreInst(iMatLI, iMat2AI, pBB);
  LoadInst* swapMatLI = new LoadInst(swapMatAI,"", pBB);
  StoreInst* mallocSI3 = new StoreInst(swapMatLI, iMatAI, pBB);

  BranchInst::Create(forCondBB, pBB);

  LoadInst* iLI = new LoadInst((Value*) iAI, "", forCondBB);
  LoadInst* lenLoadInst = new LoadInst((Value*) lenAllocaInst, "", forCondBB);
  ICmpInst* forII = new ICmpInst(*forCondBB,CmpInst::ICMP_SLT,(Value*) iLI, (Value*) lenLoadInst,"");
  BranchInst::Create(forBodyBB,forEndBB, forII, forCondBB);

  //For body

  LoadInst* iFbLI = new LoadInst((Value*) iAI, "", forBodyBB);
  BinaryOperator* shlFbBO = BinaryOperator::Create(Instruction::Shl, (Value*) ci1_64, iFbLI, "", forBodyBB);
  BinaryOperator* andFbBO = BinaryOperator::Create(Instruction::And, (Value*) inpVar, shlFbBO, "", forBodyBB);
  BinaryOperator* ashrFbBO = BinaryOperator::Create(Instruction::AShr, (Value*) andFbBO, iFbLI, "", forBodyBB);
  StoreInst* matIdFbSI = new StoreInst((Value *) ashrFbBO, matIdAI, forBodyBB);

  LoadInst* matIdFbLI = new LoadInst((Value *) matIdAI, "", forBodyBB);
  LoadInst* iFbLI02 = new LoadInst((Value*) iAI, "", forBodyBB);

  PrintInIR(module, forBodyBB, strArg_IdLI, sizeof(strArg_IdLI), matIdFbLI);

  vector<Value*> vecFb0I0;
  vecFb0I0.push_back(ci0_64);
  vecFb0I0.push_back(matIdFbLI);
  ArrayRef<Value*> arFb0I0(vecFb0I0);

  vector<Value*> vecFb0I1;
  vecFb0I1.push_back(ci0_64);
  vecFb0I1.push_back(iFbLI02);
  ArrayRef<Value*> arFb0I1(vecFb0I1);

  GetElementPtrInst* getBinFbEPI = GetElementPtrInst::CreateInBounds(matAI, matIdFbLI,"", forBodyBB);
  GetElementPtrInst* getLenFbEPI = GetElementPtrInst::CreateInBounds(getBinFbEPI, iFbLI02,"", forBodyBB);

  LoadInst* ldFbMatLI = new LoadInst(getLenFbEPI,"",forBodyBB);
  LoadInst* iMatFbLI = new LoadInst(iMatAI,"",forBodyBB);
  LoadInst* iMat2FbLI = new LoadInst(iMat2AI,"",forBodyBB);

  vector<Value*> vecFbMM;
  vecFbMM.push_back(iMatFbLI);
  vecFbMM.push_back(ldFbMatLI);
  vecFbMM.push_back(iMat2FbLI);
  vecFbMM.push_back(ci1_64);
  vecFbMM.push_back(ciDim);
  vecFbMM.push_back(ciDim);
  vecFbMM.push_back(ciDim);
  vecFbMM.push_back(ciMod);
  ArrayRef<Value*> arFbMM(vecFbMM);
  CallInst* mmFbCI = CallInst::Create(multMatFunc, arFbMM, "", forBodyBB);
  StoreInst* mallocFbSI1 = new StoreInst(iMat2FbLI, swapMatAI, forBodyBB);
  StoreInst* mallocFbSI2 = new StoreInst(iMatFbLI, iMat2AI, forBodyBB);
  LoadInst* swapMatFbLI = new LoadInst(swapMatAI,"", forBodyBB);
  StoreInst* mallocFbSI3 = new StoreInst(swapMatFbLI, iMatAI, forBodyBB);
  BranchInst::Create(forIncBB, forBodyBB);
  
  //For inc 
  LoadInst* iFiLI = new LoadInst((Value*) iAI, "", forIncBB);
  BinaryOperator* iFiAdd = BinaryOperator::CreateNSWAdd((Value*) iFiLI, ci1_64, "", forIncBB);
  StoreInst* iFiSI = new StoreInst((Value*) iFiAdd, iAI, forIncBB);
  BranchInst::Create(forCondBB, forIncBB);

  //Continue
  BranchInst* oriBI = (BranchInst*) conBB->getTerminator();
  BasicBlock* trueBB = oriBI->getSuccessor(0);
  BasicBlock* falseBB = oriBI->getSuccessor(1);
  oriBI->eraseFromParent();

  GetElementPtrInst* tailMatEPI = GetElementPtrInst::CreateInBounds(tailMatIR->getMatAI(), ci0_64, " ", conBB);
  BitCastInst* tailMatBI = new BitCastInst((Value*) tailMatEPI, l2Ptr_64, "", conBB);
  AllocaInst* tailMatAI = new AllocaInst(l2Ptr_64,"", conBB);
  StoreInst* tailMatSI = new StoreInst(tailMatBI, (Value *) tailMatAI, conBB);
  LoadInst* tailMatLI = new LoadInst(tailMatAI,"", conBB);

  LoadInst* iMatConLI = new LoadInst(iMatAI,"",conBB);
  LoadInst* iMat2ConLI = new LoadInst(iMat2AI,"",conBB);

  vector<Value*> vecConMM;
  vecConMM.push_back(iMatConLI);
  vecConMM.push_back(tailMatLI);
  vecConMM.push_back(iMat2ConLI);
  vecConMM.push_back(ci1_64);
  vecConMM.push_back(ciDim);
  vecConMM.push_back(ciDim);
  vecConMM.push_back(ci1_64);
  vecConMM.push_back(ciMod);
  ArrayRef<Value*> arConMM(vecConMM);
  CallInst* mmConCI = CallInst::Create(multMatFunc, arConMM, "", conBB);
  
  AllocaInst* cmpAI = new AllocaInst(i64Type,"cmpAI", conBB);
  LoadInst* iMatConLI2 = new LoadInst(iMat2AI,"",conBB);
  GetElementPtrInst* getCmpL1EPI = GetElementPtrInst::CreateInBounds(iMatConLI2, ci0_64, " ", conBB);
  LoadInst* getCmpL1LI = new LoadInst(getCmpL1EPI,"",conBB);
  GetElementPtrInst* getCmpEPI = GetElementPtrInst::CreateInBounds(getCmpL1LI, ci0_64, " ", conBB);
  LoadInst* getCmpLI = new LoadInst(getCmpEPI,"",conBB);
  StoreInst* cmpSI = new StoreInst((Value*) getCmpLI, cmpAI, "", conBB);
  LoadInst* cmpLI = new LoadInst(cmpAI,"",conBB);

  const char strArg1[] = "ICmp Reuslt: %d\n";
  PrintInIR(module, conBB, strArg1, sizeof(strArg1), cmpLI);

  ICmpInst* conII = new ICmpInst(*conBB, CmpInst::ICMP_EQ, (Value*) cmpLI, ci1_64, "");
  BranchInst::Create(trueBB, falseBB, conII, conBB);

  free(headMat[0]);
  free(headMatRand[0]);
  for(int64_t i=0; i<dim; i++){
    free(tailMat[i]);
    free(tailMatRand[i]);
	free(midMat0[i]);
	free(midMat0Rand[i]);
	free(midMat1[i]);
	free(midMat1Rand[i]);
  }
  free(headMat);
  free(headMatRand);
  free(tailMat);
  free(tailMatRand);
  free(midMat0);
  free(midMat0Rand);
  free(midMat1);
  free(midMat1Rand);
}
