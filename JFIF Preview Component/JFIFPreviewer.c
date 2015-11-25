/*	File:		JFIFPreviewer.c	Contains:		Written by: 		Copyright:	Copyright � 1984-1999 by Apple Computer, Inc., All Rights Reserved.				You may incorporate this Apple sample source code into your program(s) without				restriction. This Apple sample source code has been provided "AS IS" and the				responsibility for its operation is yours. You are not permitted to redistribute				this Apple sample source code as "Apple sample source code" after having made				changes. If you're going to re-distribute the source, we require that you make				it clear in the source that the code was descended from Apple sample source				code, but that you've made changes.	Change History (most recent first):				8/16/1999	Karl Groethe	Updated for Metrowerks Codewarror Pro 2.1				*/#include <Aliases.h>#include <Files.h>#include <Errors.h>#include <ToolUtils.h>#include <Memory.h>#include <QDOffscreen.h>#include <Components.h>#include <ImageCompression.h>#include <QuicktimeComponents.h>#include <FixMath.h>enum{		uppCanDoSelectorProcInfo=kPascalStackBased		 | RESULT_SIZE(SIZE_CODE(sizeof(ComponentResult)))		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(short))),		 uppshowJFIFPreviewProcInfo=kPascalStackBased		 | RESULT_SIZE(SIZE_CODE(sizeof(ComponentResult)))		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(OSType)))		 | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(PicHandle)))		 | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(Rect*))),		 uppmakeJFIFPreviewProcInfo=kPascalStackBased		 | RESULT_SIZE(SIZE_CODE(sizeof(ComponentResult)))		 | STACK_ROUTINE_PARAMETER(1,SIZE_CODE(sizeof(OSType*)))		 | STACK_ROUTINE_PARAMETER(2,SIZE_CODE(sizeof(Handle*)))		 | STACK_ROUTINE_PARAMETER(3,SIZE_CODE(sizeof(const FSSpec*)))		 | STACK_ROUTINE_PARAMETER(4,SIZE_CODE(sizeof(ICMProgressProcRecordPtr)))};pascal ComponentResult PreviewShowData(pnotComponent p, OSType dataType, Handle data,		const Rect *inHere){	#pragma unused(p,dataType,data,inHere)	ComponentCallNow(1, 12);	return 0;}/*	NOTE: to install this previewer ( or any other components ) inside your application:		put all the resources into your app then do this:		{		Handle h;		short i,c;		short myAppFile;			// the res file refnum of your app = current res file at launch		Boolean everyBody = false;	// set to true to let other apps use component 				UseResFile(myAppFile);		c = Count1Resources('thng');		for ( i=1; i <= c; i++ )			h = Get1IndResource('thng',i);			RegisterComponentResource((ComponentResourceHandle)h,everyBody);			ReleaseResource(h);		}	}	*/#define	DEFAULT_COMPRESSION		'rpza'				// compressor for created previews - zero for uncompressed#define	DEFAULT_QUALITY			codecHighQuality	// quality level for created previews#define	SBSIZE					2048			// size of scan buffer for scanning for JFIF header/***********	struct for deep progressProc.	***********/typedef	 struct {	ICMProgressProcRecordPtr	progress;	GDHandle	gd;	CGrafPtr	port;	Fixed	start,end;} adpRec;/***********	struct for spooling with dataProcs.	***********/typedef	struct {	Ptr		bufEnd;	Ptr		bufStart;	long	bufSize;	long	size;	short	refNum;} DPRec;/***********		function prototypes	**********/pascal ComponentResult JFIFPreviewDispatch( ComponentParameters *params, Handle storage );pascal ComponentResult showJFIFPreview(OSType dataType, PicHandle data, const Rect *inHere);pascal ComponentResult makeJFIFPreview(OSType *previewType, Handle *previewResult,					const FSSpec *sourceFile, ICMProgressProcRecordPtr progress);pascal ComponentResult	CanDoSelector(short selector);pascal OSErr	adpProc(short msg,Fixed pct,long refcon);pascal OSErr	dataProc(Ptr *cp,long sizeNeeded,long refcon);OSErr	ReadJFIFThumbnail(short refNum, Handle *thumbnail, ICMProgressProcRecordPtr progress, Boolean thumbOnly);/***************************************************	Component entry point.***************************************************/#ifdef powercProcInfoType __procinfo=kPascalStackBased		 | RESULT_SIZE(SIZE_CODE(sizeof(ComponentResult)))		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(ComponentParameters*)))		 | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(Handle)));#endifpascal ComponentResult JFIFPreviewDispatch( ComponentParameters *params, Handle storage ){#pragma unused(storage)	ComponentFunctionUPP	componentProc = 0;	ComponentResult err = 0;	switch (params->what) {		case kComponentOpenSelect:#ifdef THINK_C			SetComponentInstanceA5((ComponentInstance)params->params[0], *(long *)CurrentA5);#endif				break;		case kComponentCloseSelect:			break;		case kComponentCanDoSelect:			return CallComponentFunction(params,NewRoutineDescriptor((ProcPtr)CanDoSelector, uppCanDoSelectorProcInfo, GetCurrentArchitecture()));				break;		case kComponentVersionSelect:			return 0;			break;		case 1:				componentProc = NewRoutineDescriptor((ProcPtr)showJFIFPreview,uppshowJFIFPreviewProcInfo,GetCurrentArchitecture());			break;		case 2: 			componentProc = NewRoutineDescriptor((ProcPtr)makeJFIFPreview,uppmakeJFIFPreviewProcInfo,GetCurrentArchitecture());			break;	}	if (componentProc)		err = CallComponentFunction(params, componentProc);	return err;}/***************************************************	Indicate whether we can handle the call or not.	***************************************************/pascal ComponentResultCanDoSelector(short selector){		switch (selector) {	case kComponentOpenSelect:	case kComponentCloseSelect:	case kComponentCanDoSelect:	case kComponentVersionSelect: 	case 1:	case 2:		return(true);	default:		return (false);	}}/***************************************************	We are asked to make a thumbnail from a JFIF file. We comply.***************************************************/	pascal ComponentResult makeJFIFPreview(OSType *previewType, Handle *previewResult,					const FSSpec *sourceFile, ICMProgressProcRecordPtr progress){	OSErr err;	Handle thumbnail = 0;	short refNum;	err = FSpOpenDF(sourceFile, fsRdPerm, &refNum);	*previewResult = nil;	if (!err) {		err = ReadJFIFThumbnail(refNum, previewResult, progress, false);		FSClose(refNum);	}bail:	if (!err) {		*previewType = 'PICT';	} else if ( *previewResult ) {		DisposeHandle(*previewResult);		*previewResult = nil;	}	return(err);}/***************************************************	Called to show the preview. Data is a picture, type is 'PICT';	***************************************************/pascal ComponentResult showJFIFPreview(OSType dataType, PicHandle data, const Rect *inHere){	OSErr err = noErr;	Handle thumbnail = 0;	short refNum;	FSSpec theFile;	Boolean whoCares;	Handle thePict = nil;	ComponentInstance ci;	if (dataType != rAliasType)		return paramErr;	if (err = ResolveAlias(nil, (AliasHandle)data, &theFile, &whoCares)) goto bail;	err = FSpOpenDF(&theFile, fsRdPerm, &refNum);	if (!err) {		err = ReadJFIFThumbnail(refNum, &thePict, nil, true);		FSClose(refNum);	}	if (err) goto bail;	if (ci = OpenDefaultComponent('pnot', 'PICT')) {		PreviewShowData(ci, 'PICT', thePict, inHere);		CloseComponent(ci);	}	KillPicture((PicHandle)thePict);bail:	return err;}/***************************************************	This does the work. It checks for a JFIF thumbnail and if there is one,	it makes a PICT out of it. If not it draws the JFIF file into a 96x96 (max)	offscreen and makes a PICT out of that.***************************************************/OSErrReadJFIFThumbnail(short refNum, Handle *thumbnail, ICMProgressProcRecordPtr progress, Boolean thumbOnly){	long	refcon = 0;	unsigned char buf[SBSIZE];	long l = SBSIZE;	short i;	short	j,w = 0,h = 0;	Rect	frame;	PicHandle pict = nil;	GWorldPtr	gw = nil;	CGrafPtr savePort;	GDHandle	saveGD;	unsigned char *bp,*rp;	short	rb;	char	mmuMode = 1;	char	oldMMUMode;	short x,y;	Ptr		tp;	short 	err = 0;	short	numCom = 0;	short	hv1 =0,hv2=0,hv3=0;		/* call the progress proc if we have to. */		if ( progress)  {		if ( CallICMProgressProc(progress->progressProc,codecProgressOpen,0,progress->progressRefCon) ) {			err = -1;			goto done;		}		if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x1,progress->progressRefCon) ) {			err = -1;			goto done;		}	}		GetGWorld(&savePort,&saveGD);	FSRead(refNum,&l,(Ptr)buf);		/* scan the JFIF stream for a JFIF header, or a SOF header */		for ( i=0; i < SBSIZE; i++ ) {		if ( buf[i] == 0xff ) {			i++;						/* JFIF header */						if ( buf[i] == (unsigned char)0xe0 ) {				i++;				j = (buf[i] << 8) | buf[i+1];				i++;				if ( j <= 16 )					/* no thumbnail - keep scanning */					continue;				if ( buf[i+1] == 'J'  &&					buf[i+2] == 'F'  &&					buf[i+3] == 'I'  &&					buf[i+4] == 'F' &&					buf[i+5] == 0 &&					buf[i+6] == 1 &&					buf[i+7] <= 1 ) {					w = buf[i+13];					h = buf[i+14];					if ( w == 0 || h == 0 )		/* no thumbnail - keep scanning */						continue;					else {											/* read in the thumbnail - its 8-8-8 rgb for w*h pixels */												l = w*h*3;						SetFPos(refNum,fsFromStart,(long)i+15);						i = 0;						tp = NewPtr(l);						if ( tp == nil ) {							err = -108;							goto done;						}						if ( progress)  {							if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x0100,progress->progressRefCon) ) {								err = -1;								goto done;							}						}						FSRead(refNum,&l,tp);						SetRect(&frame,0,0,w,h);												/* make a 16-bit gworld and copy the thumbnail into it */												if ( NewGWorld(&gw,16,&frame,nil,nil,0) == 0 || 							 NewGWorld(&gw,16,&frame,nil,nil,8) == 0 ) {							LockPixels(gw->portPixMap);							bp = (unsigned char *)GetPixBaseAddr(gw->portPixMap);							rb = (*gw->portPixMap)->rowBytes & 0x3fff;							oldMMUMode = GetMMUMode();							SwapMMUMode(&mmuMode);							for ( y=0; y < h; y++ ) {								rp = bp;								if ( progress) { 									SwapMMUMode(&oldMMUMode);									if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x1000 + (0x9000  * y) / h,progress->progressRefCon) ) {										err = -1;										goto done;									}									SwapMMUMode(&oldMMUMode);								}								for ( x=0; x < w; x++ ) {									short pix = (0x1f & (tp[i++]>>3)) << 10;									pix |= (0x1f & (tp[i++]>>3)) << 5;									pix |= (0x1f & (tp[i++]>>3));									*(short *)rp = pix;									rp += 2;								}								bp += rb;							}							SwapMMUMode(&mmuMode);							DisposePtr(tp);							break;			/* okay we got it - get outof the scan loop */						} else {							err = -108;							goto done;						}					}				} else {					err = -108;		// no memory					goto done;				}								/* start of frame header - grab the width and height */							} else if ( buf[i] == (unsigned char)0xc0 ) {				i += 4;				h = (buf[i]<<8) | buf[i+1];				i += 2;				w = (buf[i]<<8) | buf[i+1];				i += 2;				numCom = buf[i];				if ( numCom == 3 ) {					i += 2;					hv1 = buf[i];					i += 3;					hv2 = buf[i];					i += 3;					hv3 = buf[i];				}					break;			}		}	}	/* couldn't find a SOF header - so bail */		if ( w == 0 || h == 0 ) {		err = -50;		goto done;	}		/* there was no thumbnail - so draw the whole image */		if ( gw == nil ) {		ImageDescriptionHandle desc = nil;		ICMDataProcRecord dataP,*dataPP = nil;		DPRec dataRec;		ICMProgressProcRecord pproc;		adpRec adpRec;		MatrixRecord matrix;		short	tw,th;		Rect	tframe;		Ptr	buffer = nil;				if (thumbOnly) {			err = -50;			goto done;		}		if ( (desc = (ImageDescriptionHandle)NewHandle(sizeof(ImageDescription))) == nil ) {			err = MemError();			goto cbail;		}		/* make up an image description */				(*desc)->idSize = sizeof(ImageDescription);		(*desc)->temporalQuality = 0;		(*desc)->spatialQuality = codecNormalQuality;		(*desc)->dataSize = l;		(*desc)->cType = 'jpeg';		(*desc)->version = 0;		(*desc)->revisionLevel = 0;		(*desc)->vendor = 0;		(*desc)->hRes = 72L<<16;		(*desc)->vRes = 72L<<16;		(*desc)->width = w;		(*desc)->height = h;		(*desc)->depth = 32;		(*desc)->clutID = -1;		BlockMove("\pPhoto - JPEG",(*desc)->name,13);				if ( w > h ) {			tw = 96;			th = h * 96 / w ;		} else {			th = 96;			tw = w * 96 / h ;		}		SetRect(&tframe,0,0,tw,th);		SetRect(&frame,0,0,w,h);				/* make a gworld up to 96x96 pixels and draw the JFIF file in it */				if ( NewGWorld(&gw,16,&tframe,nil,nil,0) == 0 || 			 NewGWorld(&gw,16,&tframe,nil,nil,8) == 0 ) {			 			 			if ( progress ) {							/* make a progressproc to call for partial progress */								adpRec.progress = progress;				adpRec.port = savePort;				adpRec.gd = saveGD;				adpRec.start = 0x1000;				adpRec.end = 0xa000;								pproc.progressProc = NewICMProgressProc(adpProc);				pproc.progressRefCon = (long)&adpRec;				if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x0100,progress->progressRefCon) ) {					err = -1;					goto cbail;				}			}						/* read in file */						GetEOF(refNum,&l);			if ( (buffer = NewPtr(l)) == nil ) {										/* if we cant fit the whole thing in memory - make a dataProc to spool it in */								if ( (buffer= NewPtr(codecMinimumDataSize)) == nil ) {					err = MemError();					goto cbail;				}				dataRec.refNum = refNum;				dataRec.bufStart = buffer;				dataRec.bufEnd = buffer + codecMinimumDataSize;				dataRec.size = l - codecMinimumDataSize;				dataRec.bufSize = codecMinimumDataSize;				dataPP = &dataP;				dataP.dataRefCon = (long)&dataRec;				dataP.dataProc = NewICMDataProc(dataProc);				l = codecMinimumDataSize;			}			if ( progress ) {				if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x0800,progress->progressRefCon) ) {					err = -1;					goto cbail;				}			}			SetFPos(refNum,fsFromStart,0);			if ( (err=FSRead(refNum,&l,buffer)) != 0 ) 				goto cbail;			if ( progress ) {				if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x1000,progress->progressRefCon) ) {					err = -1;					goto cbail;				}			}			RectMatrix(&matrix,&frame,&tframe);			SetGWorld(gw,nil);			err=FDecompressImage(buffer,desc,gw->portPixMap,						&frame,&matrix,ditherCopy,(RgnHandle)nil,						(PixMapHandle)nil,(Rect *)nil,codecHighQuality,anyCodec,0,						dataPP,progress ?  &pproc : nil);			frame = tframe;cbail:			SetGWorld(savePort,saveGD);			if ( buffer )				DisposePtr(buffer);			if ( desc )				DisposeHandle((Handle)desc);			if ( err )					goto done;		} else {			err = -108;			goto done;		}	}	/* if we get here than gw holds the image for the thumbnail - put it in a PICT and compress it */	if ( gw ) {		if ( progress)  {			if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0xa000,progress->progressRefCon) ) {				err = -1;				goto done;			}		}		SetGWorld(gw,nil);		pict = OpenPicture(&frame);		ClipRect(&frame);		EraseRect(&frame);		CopyBits((BitMap *)*gw->portPixMap,(BitMap *)*gw->portPixMap,			&gw->portRect,&gw->portRect,ditherCopy,nil);		ClosePicture();		SetGWorld(savePort,saveGD);		if (thumbOnly) {			*thumbnail = (Handle)pict;			goto done;		}		if ( progress)  {			if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0xb000,progress->progressRefCon) ) {				err = -1;				goto done;			}		}		DisposeGWorld(gw);		gw  = nil;		if ( GetHandleSize((Handle)pict) <= sizeof(Picture) ) {			DisposeHandle((Handle)pict);			err = -108;				// no memory		} else {			*thumbnail = NewHandle(sizeof(Picture));			if ( progress) {				if ( CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0xc000,progress->progressRefCon) ) {					err = -1;					goto done;				}			}			if ( DEFAULT_COMPRESSION != 0 ) {				if ( (err=CompressPicture(pict,(PicHandle)*thumbnail,DEFAULT_QUALITY,DEFAULT_COMPRESSION)) != 0 ) {					DisposeHandle(*thumbnail);					*thumbnail = nil;					DisposeHandle((Handle)pict);					goto done;				}				DisposeHandle((Handle)pict);			} else {				*thumbnail = (Handle)pict;			}			if ( progress) 				CallICMProgressProc(progress->progressProc,codecProgressUpdatePercent,0x10000,progress->progressRefCon);		}	}done:	if ( gw )		DisposeGWorld(gw);	SetGWorld(savePort,saveGD);	if ( progress) 		CallICMProgressProc(progress->progressProc,codecProgressClose,0,progress->progressRefCon);	return(err);}/***************************************************	Deep progressproc - scales calls from proc and passes them up to higher level.	***************************************************/pascal OSErradpProc(short msg,Fixed pct,long refcon) {	OSErr	res = 0;	CGrafPtr savePort;	GDHandle	saveGD;	adpRec *adpr = (adpRec *)refcon;		if ( msg == codecProgressUpdatePercent ) {		pct = adpr->start + FixMul(pct,adpr->end-adpr->start);		GetGWorld(&savePort,&saveGD);		SetGWorld(adpr->port,adpr->gd);		res = CallICMProgressProc(adpr->progress->progressProc,msg,pct,adpr->progress->progressRefCon);		SetGWorld(savePort,saveGD);	}	return(res);}/***************************************************	DataProc for spooling in compressed data.	***************************************************/pascal OSErrdataProc(Ptr *cp,long sizeNeeded,long refcon){	char 	mode = 0;	Ptr		current;	long	newChunk;	long	leftOver;	long	size;	OSErr 	result = noErr;	DPRec 	*dpr = (DPRec *)refcon;		if ( cp == nil ) 		return(-1);			/* dont do random access yet */			current = *cp;	SwapMMUMode(&mode);	if ( (current + sizeNeeded) > dpr->bufEnd )  {			// move whats left up to the front of the buffer				leftOver = dpr->bufEnd - current;		BlockMove(current,dpr->bufStart,leftOver);				newChunk = dpr->bufSize - leftOver;		if ( dpr->size < newChunk )			newChunk = dpr->size;		if ( newChunk ) {			size = newChunk;			FSRead(dpr->refNum,&size,dpr->bufStart+leftOver);			dpr->size -= newChunk;			dpr->bufEnd = dpr->bufStart+leftOver+newChunk;		}		*cp = dpr->bufStart;	}	SwapMMUMode(&mode);	return(result);}