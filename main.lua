local M = {}

local FAST_EXTENSIONS = {
	jef = true,
	["jef+"] = true,
	pes = true,
	pec = true,
	dst = true,
}

function M:peek(job)
	local start_time = os.clock()
	local cache_url = ya.file_cache(job)
	if not cache_url then
		return
	end

	local preload_ok, preload_err = self:preload(job)
	if not preload_ok or preload_err then
		return ya.preview_widget(job, preload_err)
	end

	ya.sleep(math.max(0, rt.preview.image_delay / 1000 + start_time - os.clock()))

	local _, show_err = ya.image_show(cache_url, job.area)
	ya.preview_widget(job, show_err)
end

function M:seek() end

function M:preload(job)
	local cache_url = ya.file_cache(job)
	if not cache_url or fs.cha(cache_url) then
		return true
	end

	local file_path = job.file.path and tostring(job.file.path) or tostring(job.file.url)
	local cache_path = tostring(cache_url)
	local unique_suffix = ya.hash(string.format("embroidery//%s//%.10f", cache_path, ya.time()))
	local temp_png_url, unique_err = fs.unique("file", Url(string.format("%s.%s.png", cache_path, unique_suffix)))
	if not temp_png_url then
		return true, Err("Failed to create temporary preview path: %s", unique_err)
	end
	local temp_png_path = tostring(temp_png_url)

	local extension = file_path:match("%.([^./\\]+)$")
	if extension ~= nil then
		extension = string.lower(extension)
	end

	local command_builder = nil
	if extension ~= nil and FAST_EXTENSIONS[extension] then
		command_builder = Command("embroidery-preview"):arg({ file_path, temp_png_path, "512" })
	else
		command_builder = Command("nice"):arg({
			"-n",
			"19",
			"zigzag",
			"convert",
			"--keep-overlaps",
			"--lock",
			"none",
			"--narrow-satin",
			"0",
			file_path,
			"-o",
			temp_png_path,
		})
	end

	local child, spawn_err = command_builder:stdout(Command.NULL):stderr(Command.PIPED):spawn()
	if not child then
		fs.remove("file", temp_png_url)
		return true, Err("Failed to start previewer: %s", spawn_err)
	end

	local output, wait_err = child:wait_with_output()
	if not output then
		fs.remove("file", temp_png_url)
		return true, Err("Failed to read previewer output: %s", wait_err)
	end

	if not output.status.success then
		fs.remove("file", temp_png_url)
		return true, Err("Previewer exited with code %s: %s", output.status.code, output.stderr)
	end

	local temp_file_info = fs.cha(temp_png_url)
	if not temp_file_info or temp_file_info.len == 0 then
		fs.remove("file", temp_png_url)
		return true, Err("Previewer produced empty output: %s", output.stderr)
	end

	local renamed, rename_err = fs.rename(temp_png_url, cache_url)
	if not renamed then
		fs.remove("file", temp_png_url)
		if fs.cha(cache_url) then
			return true
		end
		return true, Err("Failed to move preview to cache: %s", rename_err)
	end

	return true
end

return M
