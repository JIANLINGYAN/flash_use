/*****************************************************************************
* Product:  QF/C
* Version:  2.6
* Released: Dec 27 2003
* Updated:  Dec 17 2004
*
* Copyright (C) 2002-2004 Quantum Leaps. All rights reserved.
*
* This software may be distributed and modified under the terms of the GNU
* General Public License version 2 (GPL) as published by the Free Software
* Foundation and appearing in the file GPL.TXT included in the packaging of
* this file. Please note that GPL Section 2[b] requires that all works based
* on this software must also be made publicly available under the terms of the
* GPL ("Copyleft").
*
* Alternatively, this software may be distributed and modified under the terms
* of Quantum Leaps commercial licenses, which are designed for users who want
* to retain proprietary status of their code. This "dual-licensing" model is
* possible because Quantum Leaps owns the copyright to this source code and as
* such can license its intelectual property any number of times. The users who
* license this software under one of Quantum Leaps commercial licenses do not
* use this software under the GPL and therefore are not subject to any of its
* terms.
*
* Contact information:
* Quantum Leaps Web site:  http://www.quantum-leaps.com
* Quantum Leaps licensing: http://www.quantum-leaps.com/licensing/overview.htm
* e-mail:                  sales@quatnum-leaps.com
*
*****************************************************************************/
#ifndef cplus_h
#define cplus_h

#ifdef __cplusplus
extern "C" {
#endif

              /* Macros for declaring simple classes (without polymorphism) */
#define CLASS(name_) \
    typedef struct name_ name_; \
    struct name_ {
#define METHODS };
#define END_CLASS

       /* Abstract base class Object, root of all classes with polymorphism */
CLASS(Object)
    struct ObjectVTABLE *vptr__;                 /* private virtual pointer */
METHODS
                                       /* protected constructor 'inline'... */
    #define ObjectCtor_(me_) ((me_)->vptr__ = &theObjectVTABLE, (me_))

                                        /* protected destructor 'inline'... */
    #define ObjectXtor_(me_) ((void)0)

                               /* dummy implementation for abstract methods */
    void ObjectAbstract(void);

                                     /* Run Time Type Identification (RTTI) */
    #define ObjectIS_KIND_OF(me_, class_) \
        ObjectIsKindOf__((Object *)(me_), &the##class_##VTABLE)

    int ObjectIsKindOf__(Object const *me, void const *vtable);
END_CLASS

CLASS(ObjectVTABLE)
    ObjectVTABLE *super__;                   /* pointer to superclass' VTBL */
    void (*xtor)(Object *);                    /* public virtual destructor */
METHODS
END_CLASS

extern ObjectVTABLE theObjectVTABLE;             /* Object class descriptor */

                                         /* Macros for declaring subclasses */
#define SUBCLASS(class_, superclass_) \
    CLASS(class_) \
        superclass_ super_;

               /* Macros for defining VTABLEs and binding virtual functions */
#define VTABLE(class_, superclass_) }; \
    typedef struct class_##VTABLE class_##VTABLE; \
    extern class_##VTABLE the##class_##VTABLE; \
    struct class_##VTABLE { \
        superclass_##VTABLE super_;

#define BEGIN_VTABLE(class_, superclass_) \
    class_##VTABLE the##class_##VTABLE; \
    static ObjectVTABLE *class_##VTABLECtor(class_ *t) { \
        class_##VTABLE *me = &the##class_##VTABLE; \
        *(superclass_##VTABLE *)me = \
            *(superclass_##VTABLE *)((Object *)t)->vptr__;

#define VMETHOD(class_, meth_) ((class_##VTABLE *)me)->meth_

#define END_VTABLE \
        ((ObjectVTABLE *)me)->super__ = ((Object *)t)->vptr__; \
        return (ObjectVTABLE *)me; \
    }

                      /* Macro to hook virtual pointer used in constructors */
#define VHOOK(class_) \
    if (((ObjectVTABLE *)&the##class_##VTABLE)->super__ == 0) \
        ((Object *)me)->vptr__ = class_##VTABLECtor(me); \
    else \
       (((Object *)me)->vptr__ = (ObjectVTABLE *)&the##class_##VTABLE)

                                               /* Macro to access v-pointer */
#define VPTR(class_, obj_) \
    ((class_##VTABLE *)(((Object *)(obj_))->vptr__))

/* Macro for invoking virtual class methods and macro for invoking interface
 * methods.
 */
#define VCALL(class_, meth_, obj_) \
    (*VPTR(class_, obj_)->meth_)((class_*)(obj_)
#define END_CALL )

                               /* Find an offset of a member into its class */
// #define OFFSET_OF(class_, attr_) ((unsigned)&((class_ *)0)->attr_)           //redefined in /middleware/airoha/race_cmd/inc/race_cmd.h:211:

	/** \brief Indication definition
	*/
#define IND_EVT(event)    \
	typedef struct event {	
		//QEvt super;
	
#define END_IND_EVT(event)\
	} event;
	
	/** \brief Request event
	 */
#define REQ_EVT(req_id)    \
	typedef struct req_id {  
		//QEvt super; 		
		//QActive * sender;		
#define END_REQ_EVT(req_id)\
	} req_id;
	
	
	/** \brief Response event
	 */
#define RESP_EVT(resp_id) IND_EVT(resp_id) \
		eEvtReturn	evtReturn;
#define END_RESP_EVT(resp_id) END_IND_EVT(resp_id)


#ifdef __cplusplus
}
#endif

#endif                                                           /* cplus_h */
