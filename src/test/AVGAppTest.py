#!/usr/bin/python
# -*- coding: utf-8 -*-
# libavg - Media Playback Engine.
# Copyright (C) 2003-2008 Ulrich von Zadow
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Current versions can be found at www.libavg.de
#

import sys
import os
import time

import libavg
from libavg import avg, Point2D
import testcase

g_player = avg.Player.get()
g_helper = g_player.getTestHelper()

TEST_RESOLUTION = (160, 120)

class TestAppBase(libavg.AVGApp):
    def requestStop(self, timeout=0):
        g_player.setTimeout(timeout, g_player.stop)

    def singleKeyPress(self, char):
        g_helper.fakeKeyEvent(avg.KEYDOWN, ord(char), ord(char), char, ord(char), 
                avg.KEYMOD_NONE)
        g_helper.fakeKeyEvent(avg.KEYUP, ord(char), ord(char), char, ord(char), 
                avg.KEYMOD_NONE)


class AVGAppTestCase(testcase.AVGTestCase):
    def testMinimal(self):
        class MinimalApp(TestAppBase):
            testInstance = self
            def init(self):
                self.testInstance.assert_(not g_player.isFullscreen())
                self.requestStop()

        if 'AVG_DEPLOY' in os.environ:
            del os.environ['AVG_DEPLOY']
        MinimalApp.start(resolution=TEST_RESOLUTION)
    
    def testAvgDeploy(self):
        class FullscreenApp(TestAppBase):
            testInstance = self
            def init(self):
                self.testInstance.assert_(g_player.isFullscreen())
                rootNodeSize = g_player.getRootNode().size
                self.testInstance.assert_(rootNodeSize == TEST_RESOLUTION)
                self.requestStop()
                
        os.environ['AVG_DEPLOY'] = '1'
        FullscreenApp.start(resolution=TEST_RESOLUTION)
        del os.environ['AVG_DEPLOY']

    def testDebugWindowSize(self):
        class DebugwindowApp(TestAppBase):
            testInstance = self
            def init(self):
                self.testInstance.assert_(not g_player.isFullscreen())
                rootNodeSize = g_player.getRootNode().size
                self.testInstance.assert_(rootNodeSize == TEST_RESOLUTION)
                
                # windowSize = g_player.getWindowResolution()
                # self.testInstance.assert_(windowSize == Point2D(TEST_RESOLUTION) / 2)
                self.requestStop()
        
        DebugwindowApp.start(resolution=TEST_RESOLUTION,
                debugWindowSize=Point2D(TEST_RESOLUTION) / 2)
    
    def testScreenshot(self):
        expectedFiles = ['screenshot-000.png', 'screenshot-001.png']

        def cleanup():
            for screenshotFile in expectedFiles[::-1]:
                if os.path.exists(screenshotFile):
                    os.unlink(screenshotFile)
            
        def checkCallback():
            for screenshotFile in expectedFiles[::-1]:
                if os.path.exists(screenshotFile):
                    avg.Bitmap(screenshotFile)
                else:
                    raise RuntimeError('Cannot find the expected '
                            'screenshot file %s' % screenshotFile)
            
            g_player.stop()
            
        class ScreenshotApp(TestAppBase):
            def init(self):
                self.singleKeyPress('s')
                self.singleKeyPress('s')
                self.timeStarted = time.time()
                self.timerId = g_player.setOnFrameHandler(self.onFrame)
            
            def onFrame(self):
                if (os.path.exists(expectedFiles[-1]) or
                        time.time() - self.timeStarted > 1):
                    g_player.clearInterval(self.timerId)
                    checkCallback()
        
        cleanup()
        ScreenshotApp.start(resolution=TEST_RESOLUTION)
        cleanup()
    
    def testGraphs(self):
        class GraphsApp(TestAppBase):
            def init(self):
                self.enableGraphs()
            
            def enableGraphs(self):
                self.singleKeyPress('f')
                self.singleKeyPress('m')
                g_player.setTimeout(500, self.disableGraphs)
                
            def disableGraphs(self):
                self.singleKeyPress('m')
                self.singleKeyPress('f')
                self.requestStop()
        
        GraphsApp.start(resolution=TEST_RESOLUTION)
    
    def testClicktest(self):
        STATE_WAITING_FIRST_EVENT = 'STATE_WAITING_FIRST_EVENT'
        STATE_DISCARDING_EVENTS = 'STATE_DISCARDING_EVENTS'
        STATE_EXPECTING_NO_EVENTS = 'STATE_EXPECTING_NO_EVENTS'
        
        class ClicktestApp(TestAppBase):
            def init(self):
                button = avg.RectNode(size=TEST_RESOLUTION, parent=self._parentNode)
                button.connectEventHandler(avg.CURSORDOWN, avg.MOUSE, self,
                        self.trigger)
                
                self.singleKeyPress('.')
                self.failTimerId = g_player.setTimeout(500, self.fail)
                self.state = STATE_WAITING_FIRST_EVENT
            
            def disableClicktest(self):
                g_player.clearInterval(self.failTimerId)
                del self.failTimerId
                self.singleKeyPress('.')
                self.state = STATE_DISCARDING_EVENTS
                g_player.setTimeout(200, self.listenAgain)
            
            def listenAgain(self):
                self.state = STATE_EXPECTING_NO_EVENTS
                g_player.setTimeout(200, g_player.stop)
                
            def trigger(self, event):
                if self.state == STATE_WAITING_FIRST_EVENT:
                    self.disableClicktest()
                elif self.state == STATE_EXPECTING_NO_EVENTS:
                    raise RuntimeError('Clicktest failed to deactivate')
                
            def fail(self):
                raise RuntimeError('No CURSORDOWN from the clicktest detected')
        
        ClicktestApp.start(resolution=TEST_RESOLUTION)
    
    def testToggleKeys(self):
        TOGGLE_KEYS = ['?', 't']
        class ToggleKeysApp(TestAppBase):
            def init(self):
                self.keys = TOGGLE_KEYS[:]
                g_player.setTimeout(0, self.nextKey)
            
            def nextKey(self):
                if not self.keys:
                    g_player.stop()
                else:
                    key = self.keys.pop()
                    self.singleKeyPress(key)
                    g_player.setTimeout(0, self.nextKey)
    
        ToggleKeysApp.start(resolution=TEST_RESOLUTION)
    
    def testFakeFullscreen(self):
        class FakeFullscreenApp(TestAppBase):
            fakeFullscreen = True
            def init(self):
                g_player.setTimeout(0, g_player.stop)
                
        if os.name == 'nt':
            FakeFullscreenApp.start(resolution=TEST_RESOLUTION)
        else:
            self.assertException(
                    lambda: FakeFullscreenApp.start(resolution=TEST_RESOLUTION))
        
def avgAppTestSuite(tests):
    availableTests = (
            'testMinimal',
            'testAvgDeploy',
            'testDebugWindowSize',
            'testScreenshot',
            'testGraphs',
            'testClicktest',
            'testToggleKeys',
            'testFakeFullscreen',
    )
    return testcase.createAVGTestSuite(availableTests, AVGAppTestCase, tests)
