#include <fitz.h>

fz_error *fz_rendercolortext(fz_renderer*, fz_textnode*, fz_colornode*, fz_matrix);
fz_error *fz_rendercolorpath(fz_renderer*, fz_pathnode*, fz_colornode*, fz_matrix);
fz_error *fz_rendertext(fz_renderer*, fz_textnode*, fz_matrix);
fz_error *fz_renderpath(fz_renderer*, fz_pathnode*, fz_matrix);

fz_error *fz_rendercolorimage(fz_renderer*, fz_imagenode*, fz_colornode*, fz_matrix);
fz_error *fz_renderimage(fz_renderer*, fz_imagenode*, fz_matrix);

fz_error *
fz_newrenderer(fz_renderer **gcp, fz_colorspace *processcolormodel, int gcmem)
{
	fz_error *error;
	fz_renderer *gc;

	gc = *gcp = fz_malloc(sizeof(fz_renderer));
	if (!gc)
		return fz_outofmem;

	gc->model = processcolormodel;
	gc->cache = nil;
	gc->gel = nil;
	gc->ael = nil;
	gc->tmp = nil;
	gc->acc = nil;
	gc->hasrgb = 0;

	error = fz_newglyphcache(&gc->cache, gcmem / 32, gcmem);
	if (error)
		goto cleanup;

	error = fz_newgel(&gc->gel);
	if (error)
		goto cleanup;

	error = fz_newael(&gc->ael);
	if (error)
		goto cleanup;

	return nil;

cleanup:
	if (gc->cache)
		fz_dropglyphcache(gc->cache);
	if (gc->gel)
		fz_dropgel(gc->gel);
	if (gc->ael)
		fz_dropael(gc->ael);
	fz_free(gc);

	return error;
}

void
fz_droprenderer(fz_renderer *gc)
{
	if (gc->cache)
		fz_dropglyphcache(gc->cache);
	if (gc->gel)
		fz_dropgel(gc->gel);
	if (gc->ael)
		fz_dropael(gc->ael);
	if (gc->tmp)
		fz_droppixmap(gc->tmp);
	if (gc->acc)
		fz_droppixmap(gc->acc);
	fz_free(gc);
}

fz_error *
fz_rendercolor(fz_renderer *gc, fz_colornode *color, fz_matrix ctm)
{
	fz_error *error;
	int x, y, w, h;
	float rgb[3];
	unsigned char *p;

	assert(gc->model);

	fz_convertcolor(color->cs, color->samples, gc->model, rgb);
	gc->r = rgb[0] * 255;
	gc->g = rgb[1] * 255;
	gc->b = rgb[2] * 255;

	x = gc->clip.min.x;
	y = gc->clip.min.y;
	w = gc->clip.max.x - gc->clip.min.x;
	h = gc->clip.max.y - gc->clip.min.y;

	error = fz_newpixmap(&gc->tmp, x, y, w, h, 4);
	if (error)
		return error;

	p = gc->tmp->samples;

	for (y = 0; y < gc->tmp->h; y++)
	{
		for (x = 0; x < gc->tmp->w; x++)
		{
			*p++ = 255;
			*p++ = gc->r;
			*p++ = gc->g;
			*p++ = gc->b;
		}
	}

	return nil;
}

fz_error *
fz_renderover(fz_renderer *gc, fz_overnode *over, fz_matrix ctm)
{
	fz_error *error;
	fz_node *child;
	int cluster = 0;;

	if (!gc->acc)
	{
		int x = gc->clip.min.x;
		int y = gc->clip.min.y;
		int w = gc->clip.max.x - gc->clip.min.x;
		int h = gc->clip.max.y - gc->clip.min.y;

		error = fz_newpixmap(&gc->acc, x, y, w, h, gc->model ? 4 : 1);
		if (error)
			return error;

		fz_clearpixmap(gc->acc);

		cluster = 1;
	}

	for (child = over->super.first; child; child = child->next)
	{
		error = fz_rendernode(gc, child, ctm);
		if (error)
			return error;

		if (gc->tmp)
		{
			fz_blendover(gc->tmp, gc->acc);
			fz_droppixmap(gc->tmp);
			gc->tmp = nil;
		}
	}

	if (cluster)
	{
		gc->tmp = gc->acc;
		gc->acc = nil;
	}

	return nil;
}

fz_error *
fz_rendermask(fz_renderer *gc, fz_masknode *mask, fz_matrix ctm)
{
	fz_error *error;
	fz_pixmap *oldacc;
	fz_pixmap *colorpix;
	fz_pixmap *shapepix;
	fz_node *color;
	fz_node *shape;
	fz_irect newclip;
	fz_irect oldclip;
	int x, y, w, h;

	shape = mask->super.first;
	color = shape->next;

	if (gc->acc)
	{
		if (fz_ispathnode(shape) && fz_iscolornode(color))
			return fz_rendercolorpath(gc, (fz_pathnode*)shape, (fz_colornode*)color, ctm);
		if (fz_istextnode(shape) && fz_iscolornode(color))
			return fz_rendercolortext(gc, (fz_textnode*)shape, (fz_colornode*)color, ctm);
		if (fz_isimagenode(shape) && fz_iscolornode(color))
			return fz_rendercolorimage(gc, (fz_imagenode*)shape, (fz_colornode*)color, ctm);
	}

	oldacc = gc->acc;
	oldclip = gc->clip;
	newclip = fz_roundrect(fz_boundnode(shape, ctm));
	newclip = fz_intersectirects(newclip, gc->clip);

	gc->acc = nil;
	gc->clip = newclip;

	gc->tmp = nil;
	error = fz_rendernode(gc, color, ctm);
	if (error)
		return error;
	colorpix = gc->tmp;

	gc->tmp = nil;
	error = fz_rendernode(gc, shape, ctm);
	if (error)
		return error;
	shapepix = gc->tmp;

	x = gc->clip.min.x;
	y = gc->clip.min.y;
	w = gc->clip.max.x - gc->clip.min.x;
	h = gc->clip.max.y - gc->clip.min.y;

	error = fz_newpixmap(&gc->tmp, x, y, w, h, colorpix->n);
	if (error)
		return error;

	fz_clearpixmap(gc->tmp);

	fz_blendmask(gc->tmp, colorpix, shapepix);

	fz_droppixmap(shapepix);
	fz_droppixmap(colorpix);

	gc->acc = oldacc;
	gc->clip = oldclip;

	return nil;
}

fz_error *
fz_rendertransform(fz_renderer *gc, fz_transformnode *transform, fz_matrix ctm)
{
	ctm = fz_concat(transform->m, ctm);
	return fz_rendernode(gc, transform->super.first, ctm);
}

fz_error *
fz_rendernode(fz_renderer *gc, fz_node *node, fz_matrix ctm)
{
	assert(gc->tmp == nil);

	if (!node)
		return nil;

	switch (node->kind)
	{
	case FZ_NOVER:
		return fz_renderover(gc, (fz_overnode*)node, ctm);
	case FZ_NMASK:
		return fz_rendermask(gc, (fz_masknode*)node, ctm);
	case FZ_NTRANSFORM:
		return fz_rendertransform(gc, (fz_transformnode*)node, ctm);
	case FZ_NCOLOR:
		return fz_rendercolor(gc, (fz_colornode*)node, ctm);
	case FZ_NPATH:
		return fz_renderpath(gc, (fz_pathnode*)node, ctm);
	case FZ_NTEXT:
		return fz_rendertext(gc, (fz_textnode*)node, ctm);
	case FZ_NIMAGE:
		return fz_renderimage(gc, (fz_imagenode*)node, ctm);
	case FZ_NLINK:
		return fz_rendernode(gc, ((fz_linknode*)node)->tree->root, ctm);
	default:
		return nil;
	}
}

fz_error *
fz_rendertree(fz_pixmap **outp, fz_renderer *gc, fz_tree *tree, fz_matrix ctm, fz_irect bbox, int white)
{
	fz_error *error;

	gc->clip = bbox;

	if (white)
	{
		error = fz_newpixmap(&gc->acc,
			bbox.min.x, bbox.min.y,
			bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, 4);
		if (error)
			return error;
		memset(gc->acc->samples, 0xff, gc->acc->w * gc->acc->h * gc->acc->n);
	}

	error = fz_rendernode(gc, tree->root, ctm);
	if (error)
		return error;

	if (white)
	{
		*outp = gc->acc;
		gc->acc = nil;
	}
	else
	{
		*outp = gc->tmp;
		gc->tmp = nil;
	}

	return nil;
}

