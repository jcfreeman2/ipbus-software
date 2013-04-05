import wx

from uhal.gui.utilities.utilities import dynamic_loader



class MainApplication(wx.App):

    def __init__(self, guilist, redirect=True, file_name=None):

        self.guilist = guilist        
        wx.App.__init__(self, redirect, file_name)


    def OnInit(self):
        """ Load dynamically all GUIs that are needed.
        None of them is set to be the parent one, so that all of them can be independent from each other"""
        
        for gui in self.guilist:
            class_object = dynamic_loader(gui)[0]
            gui_instance = class_object(None, -1, gui.__name__)
            gui_instance.Show(True)

        return True
            


class GuiLoader:

    def __init__(self, guilist):
        self.gui_list = guilist    


    def start(self):

        output_to_window = False
        app = MainApplication(self.gui_list, output_to_window)
        app.MainLoop()


    


def loader(default=True, guilist=[]):

    if default == True:

        try:
            default_mod_obj = __import__('uhal.gui.guis', globals(), locals(), ['defaultgui'])
            def_gui_mod_obj = default_mod_obj.defaultgui
            guilist.append(def_gui_mod_obj)
            
        except ImportError, e:
            print 'FAILED to import defaultgui module', e
            
        
    return GuiLoader(guilist)
    



def test(guilist=[]):

    for gui in guilist:
        status = dynamic_loader(gui)[1]
        if status == 'FAILED':
            return status

    return 'OK'
