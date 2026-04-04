//
//  EAGLView.m
//  OpenGLES_iPhone
//
//  Created by mmalc Crawford on 11/18/10.
//  Copyright 2010 Apple Inc. All rights reserved.
//

#import "EAGLView.h"

#import <QuartzCore/QuartzCore.h>

@interface EAGLView (PrivateMethods)
- (void)createFramebuffer;
- (void)deleteFramebuffer;
@end

@implementation EAGLView

@synthesize context;

// You must implement this method
+ (Class)layerClass
{
    return [CAEAGLLayer class];
}

//The EAGL view is stored in the nib file. When it's unarchived it's sent -initWithCoder:.
- (id)initWithCoder:(NSCoder*)coder
{
    self = [super initWithCoder:coder];
	if (self) {
        CAEAGLLayer *eaglLayer = (CAEAGLLayer *)self.layer;
        
        eaglLayer.opaque = TRUE;
        eaglLayer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
                                        [NSNumber numberWithBool:FALSE], kEAGLDrawablePropertyRetainedBacking,
                                        kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat,
                                        nil];

        if ([[UIScreen mainScreen] respondsToSelector:@selector(scale)]
            && [self respondsToSelector:@selector(setContentScaleFactor:)])
        {
            viewScale = [UIScreen mainScreen].scale;
            NSLog(@"Scale is : %f\n", viewScale);
            [self setContentScaleFactor: viewScale];
            eaglLayer.contentsScale = viewScale;
        }    
    }
    
    return self;
}

- (void)dealloc
{
    [self deleteFramebuffer];    
    [context release];
    
    [super dealloc];
}

- (void)setContext:(EAGLContext *)newContext
{
    if (context != newContext) {
        [self deleteFramebuffer];
        
        [context release];
        context = [newContext retain];
        
        [EAGLContext setCurrentContext:nil];
    }
}

- (void)createFramebuffer
{
    if (context && !defaultFramebuffer) {
        [EAGLContext setCurrentContext:context];
        
        // Create default framebuffer object.
        glGenFramebuffersOES(1, &defaultFramebuffer);
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, defaultFramebuffer);
        
        // Create color render buffer and allocate backing store.
        glGenRenderbuffersOES(1, &colorRenderbuffer);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, colorRenderbuffer);
        [context renderbufferStorage:GL_RENDERBUFFER_OES fromDrawable:(CAEAGLLayer *)self.layer];
        glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_WIDTH_OES, &framebufferWidth);
        glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_HEIGHT_OES, &framebufferHeight);
        
        glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_RENDERBUFFER_OES, colorRenderbuffer);
        
        // Create depth buffer and allocate backing store
        glGenRenderbuffersOES(1, &_depthRenderBuffer);
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, _depthRenderBuffer);
        
        GLenum depthFormat = GL_DEPTH_COMPONENT16_OES;
		const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
		if (extensions && strstr(extensions, "GL_OES_depth24")) {
			depthFormat = GL_DEPTH_COMPONENT24_OES;
		}
		glRenderbufferStorageOES(GL_RENDERBUFFER_OES, depthFormat, framebufferWidth, framebufferHeight);
		glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, _depthRenderBuffer);
        
        NSLog(@"Created framebuffer with size %d, %d\n", framebufferWidth, framebufferHeight);
        
        if (glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES)
            NSLog(@"Failed to make complete framebuffer object %x", glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES));
        
		GLenum status = glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES);
		if (status != GL_FRAMEBUFFER_COMPLETE_OES) {
			NSLog(@"FRAMEBUFFER INCOMPLETE: 0x%x", status);
			// 0x8CD6 = INCOMPLETE_ATTACHMENT
			// 0x8CD7 = INCOMPLETE_MISSING_ATTACHMENT  
			// 0x8CD9 = INCOMPLETE_DIMENSIONS
		}
    }
}

- (void)deleteFramebuffer
{
    if (context) {
        [EAGLContext setCurrentContext:context];
        
        if (defaultFramebuffer) {
            glDeleteFramebuffersOES(1, &defaultFramebuffer);
            defaultFramebuffer = 0;
        }
        
        if (colorRenderbuffer) {
            glDeleteRenderbuffersOES(1, &colorRenderbuffer);
            colorRenderbuffer = 0;
        }
        if (_depthRenderBuffer) {
            glDeleteRenderbuffersOES(1, &_depthRenderBuffer);
            _depthRenderBuffer = 0;
        }
    }
}

- (void)setFramebuffer
{
    if (context) {
        [EAGLContext setCurrentContext:context];
        
        if (!defaultFramebuffer)
            [self createFramebuffer];
        
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, defaultFramebuffer);
        
        glViewport(0, 0, framebufferWidth, framebufferHeight);
    }
}

- (BOOL)presentFramebuffer
{
    BOOL success = FALSE;
    
    if (context) {
        [EAGLContext setCurrentContext:context];
        
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, colorRenderbuffer);
        
        success = [context presentRenderbuffer:GL_RENDERBUFFER_OES];
    }
    
    return success;
}

- (void)layoutSubviews
{
    // The framebuffer will be re-created at the beginning of the next setFramebuffer method call.
    [self deleteFramebuffer];
}

- (BOOL) isMultipleTouchEnabled {
    return YES;
}


@end
