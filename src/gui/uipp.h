#ifndef UIPP_H
#define UIPP_H

class UIPath
{
	class Figure
	{
	public:
		Figure(UIPath& path, double x, double y):
			_path(path)
		{
			uiDrawPathNewFigure(path._path, x, y);
		}

		Figure& lineTo(double x, double y)
		{
			uiDrawPathLineTo(_path._path, x, y);
			return *this;
		}

		UIPath& end()
		{
			uiDrawPathCloseFigure(_path._path);
			return _path;
		}

	private:
		UIPath& _path;
	};

public:
	UIPath(uiAreaDrawParams* params):
		_params(params),
		_path(uiDrawNewPath(uiDrawFillModeWinding))
	{}

	UIPath& rectangle(double x, double y, double w, double h)
	{
		uiDrawPathAddRectangle(_path, x, y, w, h);
		return *this;
	}

	Figure begin(double x, double y)
	{
		return Figure(*this, x, y);
	}

	void fill(uiDrawBrush& fillBrush)
	{
		uiDrawPathEnd(_path);
		uiDrawFill(_params->Context, _path, &fillBrush);
	}

	void stroke(uiDrawBrush& strokeBrush, uiDrawStrokeParams& strokeParams)
	{
		uiDrawPathEnd(_path);
		uiDrawStroke(_params->Context, _path, &strokeBrush, &strokeParams);
	}

	void fill(uiDrawBrush& strokeBrush, uiDrawStrokeParams& strokeParams, uiDrawBrush& fillBrush)
	{
		uiDrawPathEnd(_path);
		uiDrawFill(_params->Context, _path, &fillBrush);
		uiDrawStroke(_params->Context, _path, &strokeBrush, &strokeParams);
	}

	~UIPath()
	{
		uiDrawFreePath(_path);
	}
	
private:
	uiAreaDrawParams* _params;
	uiDrawPath* _path;
};

class UIControl
{
public:
	virtual ~UIControl()
	{
		if (_owned && _control)
			uiControlDestroy(_control);
	}

	void setControl(uiControl* control)
	{
		_control = control;
	}

	uiControl* control() const
	{
		return _control;
	}

	uiControl* claim()
	{
		_owned = false;
		return _control;
	}

	virtual UIControl* build() = 0;

	bool stretchy() const
	{
		return _stretchy;
	}

	UIControl* setStretchy(bool stretchy)
	{
		_stretchy = stretchy;
		return this;
	}

	bool built() const
	{
		return !!_control;
	}

private:
	uiControl* _control = nullptr;
	bool _owned = true;
	bool _stretchy = false;
};

template <class T, class B>
class UITypedControl : public UIControl
{
public:
	void setControl(T* control)
	{
		UIControl::setControl(uiControl(control));
	}

	T* typedControl() const
	{
		return (T*) control();
	}
};

template <class T, class B>
class UIContainerControl : public UITypedControl<T, B>
{
public:
	B* add(UIControl* child)
	{
		_children.push_back(child);
		return (B*) this;
	}

	const std::vector<UIControl*>& children() const { return _children; }

private:
	std::vector<UIControl*> _children;
};

template <class B>
class UIBox : public UIContainerControl<uiBox, B>
{
public:
	UIBox* build()
	{
		for (auto& child : this->children())
			uiBoxAppend(this->typedControl(), child->claim(), child->stretchy());
		return this;
	}
};

class UIHBox : public UIBox<UIHBox>
{
public:
	UIHBox* build()
	{
		setControl(uiNewHorizontalBox());
		UIBox::build();
		return this;
	}
};

class UIVBox : public UIBox<UIVBox>
{
public:
	UIVBox* build()
	{
		setControl(uiNewVerticalBox());
		UIBox::build();
		return this;
	}
};

class UIArea : public UITypedControl<uiArea, UIArea>
{
public:
	UIArea():
		_selfptr(this)
	{}

	UIArea* build()
	{
		setControl(uiNewArea(&_handler));
		return this;
	}

	virtual void onRedraw(uiAreaDrawParams* params) {}
	virtual void onMouseEvent(uiAreaMouseEvent* event) {}
	virtual void onMouseEntryExit(bool exit) {}
	virtual void onDragBroken() {}
	virtual int onKeyEvent(uiAreaKeyEvent* event) { return 0; }

private:
	static UIArea* _getarea(uiAreaHandler* a)
	{
		return *(UIArea**)(a+1);
	}

	static void _draw_cb(uiAreaHandler* a, uiArea* area, uiAreaDrawParams* p)
	{
		_getarea(a)->onRedraw(p);
	}

	static void _mouse_event_cb(uiAreaHandler* a, uiArea* area, uiAreaMouseEvent* event)
	{
		_getarea(a)->onMouseEvent(event);
	}

	static void _mouse_crossed_cb(uiAreaHandler* a, uiArea* area, int left)
	{
		_getarea(a)->onMouseEntryExit(!left);
	}

	static void _drag_broken_cb(uiAreaHandler* a, uiArea* area)
	{
		_getarea(a)->onDragBroken();
	}

	static int _key_event_cb(uiAreaHandler* a, uiArea* area, uiAreaKeyEvent* event)
	{
		return _getarea(a)->onKeyEvent(event);
	}

	/* These two fields must be next to each other to allow _getarea to find the area
	 * instance pointer. */
	uiAreaHandler _handler = {
		_draw_cb,
		_mouse_event_cb,
		_mouse_crossed_cb,
		_drag_broken_cb,
		_key_event_cb
	};
	UIArea* _selfptr;
};

class UIWindow : public UITypedControl<uiWindow, UIWindow>
{
public:
	UIWindow(const std::string& title, double width, double height):
		_title(title),
		_width(width),
		_height(height)
	{}

	UIWindow* build()
	{
		setControl(uiNewWindow(_title.c_str(), _width, _height, 1));
		uiWindowOnClosing(this->typedControl(), _close_cb, this);
		if (_child)
			uiWindowSetChild(this->typedControl(), _child->claim());
		return this;
	}

	UIWindow* setChild(UIControl* child)
	{
		_child = child;
		return this;
	}

	UIWindow* show()
	{
		uiControlShow(claim());
		return this;
	}

	virtual int onClose()
	{
		return 0;
	}

private:
	static int _close_cb(uiWindow*, void* ptr)
	{
		return ((UIWindow*)ptr)->onClose();
	}

private:
	std::string _title;
	double _width;
	double _height;
	UIControl* _child = nullptr;
};

class UIButton : public UITypedControl<uiButton, UIButton>
{
public:
	UIButton(const std::string& text):
		_text(text)
	{}

	UIButton* build()
	{
		setControl(uiNewButton(_text.c_str()));
		uiButtonOnClicked(this->typedControl(), _clicked_cb, this);
		return this;
	}

	virtual void onClick() {}

private:
	static void _clicked_cb(uiButton*, void* ptr)
	{
		((UIButton*)ptr)->onClick();
	}

private:
	std::string _text;
};

class UIAllocator
{
public:
	template <class T, typename... Args>
	T* make(Args&&... args)
	{
		auto uptr = std::make_unique<T>(args...);
		T* ptr = uptr.get();
		_pointers.push_back(std::move(uptr));
		return ptr;
	}

private:
	std::vector<std::unique_ptr<UIControl>> _pointers;
};

#endif
